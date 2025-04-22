using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace MultiplayerGameClient
{
    public class GameForm : Form
    {
        /* ----------  singleton « feed » depuis QueriesForm ---------- */
        private static GameForm instance;
        public static void Feed(string msg)
        {
            instance?.Enqueue(msg);
        }

        /* ----------  UI ---------- */
        private readonly Panel board;
        private readonly RichTextBox chatBox;
        private readonly TextBox input;

        /* ----------  état ---------- */
        private readonly string me;
        private readonly StreamWriter writer;
        private readonly BlockingCollection<string> inbox;

        private readonly Dictionary<string, Point> pos =
            new Dictionary<string, Point>();

        private readonly object posLock = new object();

        /* === v5 ADD: avion image === */
        private readonly Image planeImage;
        private const int PlaneSize = 40;

        /* ----------  ctor ---------- */
        public GameForm(StreamWriter w,
                        BlockingCollection<string> hostInbox,
                        string myName)
        {
            instance = this;
            writer = w;
            inbox = hostInbox;
            me = myName;

            Text = "Game";
            Size = new Size(800, 600);
            KeyPreview = true;

            // Chargement de l'image de l'avion
            string imgPath = Path.Combine(Application.StartupPath, "..", "plane.png");
            if (!File.Exists(imgPath))
            {
                MessageBox.Show($"Image not found: {imgPath}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                Close();
                return;
            }
            planeImage = Image.FromFile(imgPath);

            board = new Panel
            {
                Location = new Point(10, 10),
                Size = new Size(560, 540),
                BackColor = Color.White
            };
            board.Paint += Board_Paint;

            chatBox = new RichTextBox
            {
                ReadOnly = true,
                Location = new Point(580, 10),
                Size = new Size(200, 500)
            };

            input = new TextBox
            {
                Location = new Point(580, 520),
                Size = new Size(200, 30)
            };
            input.KeyDown += Input_KeyDown;

            Controls.AddRange(new Control[] { board, chatBox, input });

            // position initiale du joueur local
            pos[me] = new Point(50, 50);

            // consommation messages dans le thread UI
            var ui = SynchronizationContext.Current;
            Task.Run(() =>
            {
                foreach (var m in inbox.GetConsumingEnumerable())
                    if (m.StartsWith("CHAT_MSG:") ||
                        m.StartsWith("MOVE:"))
                        ui.Post(_ => Dispatch(m), null);
            });

            KeyDown += HandleArrows;
        }

        /* ----------  dispatch ---------- */
        private void Enqueue(string m) => Dispatch(m);
        private void Dispatch(string msg)
        {
            if (msg.StartsWith("CHAT_MSG:"))
                AppendChat(msg);
            else if (msg.StartsWith("MOVE:"))
                ApplyMove(msg);
        }

        /* ----------  chat ---------- */
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

        /* ----------  déplacement ---------- */
        private void HandleArrows(object s, KeyEventArgs e)
        {
            Point p;
            lock (posLock) p = pos[me];

            switch (e.KeyCode)
            {
                case Keys.Left:  p.X -= 10; break;
                case Keys.Right: p.X += 10; break;
                case Keys.Up:    p.Y -= 10; break;
                case Keys.Down:  p.Y += 10; break;
                default: return;
            }
            lock (posLock) pos[me] = p;
            board.Invalidate();

            writer.WriteLine($"MOVE:{p.X}:{p.Y}");
        }
        private void ApplyMove(string packet)
        {
            var p = packet.Split(':');
            if (p.Length != 4) return;
            var user = p[1];
            if (!int.TryParse(p[2], out int x) || !int.TryParse(p[3], out int y)) return;

            lock (posLock) pos[user] = new Point(x, y);
            board.Invalidate();
        }

        /* ----------  dessin ---------- */
        private void Board_Paint(object s, PaintEventArgs e)
        {
            lock (posLock)
            {
                foreach (var kv in pos)
                {
                    // Dessin de l'avion
                    e.Graphics.DrawImage(
                        planeImage,
                        kv.Value.X,
                        kv.Value.Y,
                        PlaneSize,
                        PlaneSize);

                    // Nom du joueur dessous
                    var textPos = new Point(kv.Value.X, kv.Value.Y + PlaneSize + 2);
                    e.Graphics.DrawString(
                        kv.Key,
                        Font,
                        Brushes.Black,
                        textPos);
                }
            }
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            instance = null;
            base.OnFormClosed(e);
        }
    }
}
