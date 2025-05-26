using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace MultiplayerGameClient
{
    public class GameForm : Form
    {
        /* ---------- singleton ---------- */
        private static GameForm instance;
        public static void Feed(string m) => instance?.Enqueue(m);

        /* ---------- UI ---------- */
        private readonly Panel board;
        private readonly Label waitLabel;
        private readonly RichTextBox chatBox;
        private readonly TextBox input;

        /* ---------- réseau & état ---------- */
        private readonly string me;
        private readonly StreamWriter writer;
        private readonly BlockingCollection<string> inbox;

        private readonly Dictionary<string, Point> pos = new Dictionary<string, Point>();
        private readonly object posLock = new object();

        /* ---------- images ---------- */
        private readonly Image planeImage;
        private readonly Image projectileImage;
        private const int PlaneSize      = 40;
        private const int ProjectileSize = 20;

        /* ---------- projectiles ---------- */
        private readonly List<Projectile> projectiles = new List<Projectile>();
        private readonly object projLock   = new object();
        private const int MaxActiveMissiles          = 5;
        private static readonly TimeSpan FireCooldown = TimeSpan.FromMilliseconds(800);
        private const int ProjectileSpeed            = 20;
        private DateTime lastFire = DateTime.MinValue;

        /* ---------- boucle jeu ---------- */
        private readonly Thread gameLoop;
        private volatile bool running = true;

        /* attribution dynamique des côtés */
        private bool isLeftSide   = true;
        private bool sideAssigned = false;

        /* ======================================================= */
        public GameForm(StreamWriter w,
                        BlockingCollection<string> hostInbox,
                        string myName)
        {
            instance = this;
            writer   = w;
            inbox    = hostInbox;
            me       = myName;

            Text = "Game";
            Size = new Size(800, 600);
            KeyPreview = true;

            /* ---------- images ---------- */
            string planePath = Path.Combine(Application.StartupPath, "..", "plane.png");
            string projPath  = Path.Combine(Application.StartupPath, "..", "projectile.png");
            if (!File.Exists(planePath) || !File.Exists(projPath))
            {
                MessageBox.Show("plane.png ou projectile.png manquant.",
                                "Erreur", MessageBoxButtons.OK, MessageBoxIcon.Error);
                Close();
                return;
            }
            planeImage      = Image.FromFile(planePath);
            projectileImage = Image.FromFile(projPath);

            /* ---------- board ---------- */
            board = new Panel
            {
                Location  = new Point(10, 10),
                Size      = new Size(560, 540),
                BackColor = Color.White,
                Visible   = false                       // caché tant qu’on attend
            };
            board.Paint += Board_Paint;
            typeof(Panel).GetProperty("DoubleBuffered",
                BindingFlags.Instance | BindingFlags.NonPublic)
                ?.SetValue(board, true, null);

            /* écran d’attente */
            waitLabel = new Label
            {
                Text      = "Waiting for opponent…",
                Location  = board.Location,
                Size      = board.Size,
                BackColor = Color.LightGray,
                TextAlign = ContentAlignment.MiddleCenter,
                Font      = new Font(FontFamily.GenericSansSerif, 14, FontStyle.Bold)
            };

            /* chat & input */
            chatBox = new RichTextBox { ReadOnly = true, Location = new Point(580, 10), Size = new Size(200, 500) };
            input   = new TextBox     { Location = new Point(580, 520), Size = new Size(200, 30) };
            input.KeyDown += Input_KeyDown;

            Controls.AddRange(new Control[] { board, waitLabel, chatBox, input });

            /* position provisoire */
            pos[me] = new Point(50, board.Height / 2);

            /* ---- thread réseau → UI ---- */
            var ui = SynchronizationContext.Current;
            Task.Run(() =>
            {
                foreach (var m in inbox.GetConsumingEnumerable())
                    if (m.StartsWith("CHAT_MSG:") ||
                        m.StartsWith("MOVE:")     ||
                        m.StartsWith("FIRE:"))
                        ui.Post(_ => Dispatch(m), null);
            });

            /* boucle jeu (démarrée plus tard) */
            gameLoop = new Thread(GameLoop) { IsBackground = true };

            KeyDown += HandleKeys;
        }

        /* === démarre la boucle + envoie la position initiale === */
        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            if (running) gameLoop.Start();

            // broadcast de la position de départ
            SendMove(pos[me]);
        }

        /* =======================================================
         *                      DISPATCH
         * ===================================================== */
        private void Dispatch(string msg)
        {
            if      (msg.StartsWith("CHAT_MSG:")) AppendChat(msg);
            else if (msg.StartsWith("MOVE:"))     ApplyMove(msg);
            else if (msg.StartsWith("FIRE:"))     ApplyFire(msg);
        }
        private void Enqueue(string m) => Dispatch(m);  /* pour Feed */

        /* ---------------- CHAT ---------------- */
        private void Input_KeyDown(object s, KeyEventArgs e)
        {
            if (e.KeyCode == Keys.Enter && input.Text != "")
            {
                writer.WriteLine($"CHAT:{input.Text}");
                input.Clear();
                e.SuppressKeyPress = true;
            }
        }
        private void AppendChat(string packet)
        {
            var p = packet.Split(new[] { ':' }, 3);
            chatBox.AppendText($"[{p[1]}] {p[2]}\n");
        }

        /* --------------- CLAVIER ---------------- */
        private void HandleKeys(object sender, KeyEventArgs e)
        {
            bool moved = false;
            Point p;
            lock (posLock) p = pos[me];

            switch (e.KeyCode)
            {
                case Keys.Left:  p.X -= 10; moved = true; break;
                case Keys.Right: p.X += 10; moved = true; break;
                case Keys.Up:    p.Y -= 10; moved = true; break;
                case Keys.Down:  p.Y += 10; moved = true; break;
                case Keys.Space: FireProjectile();         break;
                default: return;
            }

            EnforceBorders(ref p);
            if (moved)
            {
                lock (posLock) pos[me] = p;
                board.Refresh();
                SendMove(p);
            }
        }
        private void EnforceBorders(ref Point p)
        {
            int mid = board.Width / 2;
            if (sideAssigned)
            {
                if (isLeftSide) p.X = Math.Min(p.X, mid - PlaneSize);
                else            p.X = Math.Max(p.X, mid);
            }

            p.X = Math.Max(0, Math.Min(p.X, board.Width  - PlaneSize));
            p.Y = Math.Max(0, Math.Min(p.Y, board.Height - PlaneSize));
        }

        /* ------------- tir local ------------- */
        private void FireProjectile()
        {
            if (DateTime.UtcNow - lastFire < FireCooldown) return;

            int actives;
            lock (projLock) actives = projectiles.Count(pr => pr.Owner == me);
            if (actives >= MaxActiveMissiles) return;

            Point start;
            lock (posLock) start = pos[me];

            int dx = isLeftSide ? +ProjectileSpeed : -ProjectileSpeed;
            var proj = new Projectile
            {
                Owner = me,
                X  = start.X + PlaneSize / 2 - ProjectileSize / 2,
                Y  = start.Y + PlaneSize / 2 - ProjectileSize / 2,
                Dx = dx,
                Dy = 0
            };

            lock (projLock) projectiles.Add(proj);
            board.Refresh();
            lastFire = DateTime.UtcNow;

            writer.WriteLine($"FIRE:{proj.X}:{proj.Y}:{proj.Dx}:{proj.Dy}");
        }

        /* -------- FIRE réseau -------- */
        private void ApplyFire(string packet)
        {
            var p = packet.Split(':');
            if (p.Length != 6) return;

            var proj = new Projectile
            {
                Owner = p[1],
                X  = int.Parse(p[2]),
                Y  = int.Parse(p[3]),
                Dx = int.Parse(p[4]),
                Dy = int.Parse(p[5])
            };
            lock (projLock) projectiles.Add(proj);
            board.Refresh();
        }

        /* -------- MOVE réseau -------- */
        private void ApplyMove(string packet)
        {
            var p = packet.Split(':');
            if (p.Length != 4) return;
            var user = p[1];
            if (!int.TryParse(p[2], out int x) || !int.TryParse(p[3], out int y)) return;

            lock (posLock)
            {
                pos[user] = new Point(x, y);       // ajoute ou met à jour
            }

            DetermineSides();    // attribue gauche/droite dès qu'on est 2
            board.Refresh();
        }

        /* -------- Attribution des côtés -------- */
        private void DetermineSides()
        {
            lock (posLock)
            {
                if (sideAssigned || pos.Count < 2) return;

                var names = pos.Keys.OrderBy(n => n).ToList();
                isLeftSide   = names[0] == me;
                sideAssigned = true;

                /* positions de départ fixes */
                if (isLeftSide)
                {
                    pos[me]           = new Point(50, board.Height / 2);
                    pos[names[1]]     = new Point(board.Width - PlaneSize - 50, board.Height / 2);
                }
                else
                {
                    pos[me]           = new Point(board.Width - PlaneSize - 50, board.Height / 2);
                    pos[names[0]]     = new Point(50, board.Height / 2);
                }

                /* on révèle le plateau */
                waitLabel.Visible = false;
                board.Visible     = true;

                /* et on rebroadcast notre position correcte */
                SendMove(pos[me]);
            }
        }

        private void SendMove(Point p) =>
            writer.WriteLine($"MOVE:{p.X}:{p.Y}");

        /* =======================================================
         *                    BOUCLE JEU
         * ===================================================== */
        private void GameLoop()
        {
            var sw = Stopwatch.StartNew();
            const double targetDt = 1000.0 / 60;

            while (running)
            {
                if (!board.IsHandleCreated) { Thread.Sleep(10); continue; }

                double start = sw.Elapsed.TotalMilliseconds;
                AdvanceProjectiles();

                try { board.Invoke((MethodInvoker)(board.Refresh)); }
                catch (ObjectDisposedException) { return; }

                double dt = sw.Elapsed.TotalMilliseconds - start;
                int sleep = (int)(targetDt - dt);
                if (sleep > 0) Thread.Sleep(sleep);
            }
        }

        private void AdvanceProjectiles()
        {
            lock (projLock)
            {
                for (int i = projectiles.Count - 1; i >= 0; i--)
                {
                    var pr = projectiles[i];
                    pr.X += pr.Dx;

                    if (pr.X < -ProjectileSize || pr.X > board.Width)
                        projectiles.RemoveAt(i);
                    else
                        projectiles[i] = pr;
                }
            }
        }

        /* ---------------- RENDU ---------------- */
        private void Board_Paint(object s, PaintEventArgs e)
        {
            using (var pen = new Pen(Color.Gray, 2))
                e.Graphics.DrawLine(pen, board.Width / 2, 0, board.Width / 2, board.Height);

            /* ---------- projectiles ---------- */
            lock (projLock)
                foreach (var pr in projectiles)
                    DrawFlippableImage(e.Graphics, projectileImage,
                                       pr.X, pr.Y, ProjectileSize, ProjectileSize,
                                       pr.Dx < 0);    // vers la gauche → retourné

            /* ---------- avions + pseudo ---------- */
            lock (posLock)
                foreach (var kv in pos)
                {
                    DrawFlippableImage(e.Graphics, planeImage,
                                       kv.Value.X, kv.Value.Y, PlaneSize, PlaneSize,
                                       IsRightSide(kv.Value));

                    e.Graphics.DrawString(kv.Key, Font, Brushes.Black,
                                          kv.Value.X, kv.Value.Y + PlaneSize + 2);
                }
        }

        /* ---------- utils ---------- */
        private struct Projectile
        {
            public int X, Dx, Y, Dy;
            public string Owner;
        }

        // ===== AJOUTS : miroir horizontal ==========================
        private void DrawFlippableImage(Graphics g, Image img,
                                        int x, int y, int w, int h, bool flip)
        {
            if (flip)
                g.DrawImage(img, x + w, y, -w, h);  // largeur négative → miroir
            else
                g.DrawImage(img, x, y, w, h);
        }
        private bool IsRightSide(Point p) => p.X >= board.Width / 2;
        // ===========================================================

        /* ---------- fermeture ---------- */
        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            running = false;
            base.OnFormClosed(e);
            Application.ExitThread();   // ▶ vide la file de messages WinForms
        }

    }
}
