using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Content;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;

namespace ClientGame
{
    public class MatchScreen : IScreen
    {
        private readonly DogfightGame  game;
        private readonly NetworkClient net;
        private readonly string        me;          // pseudo du joueur local

        /* --------------- Ressources --------------- */
        private SpriteFont font;
        private Texture2D background;
        private Texture2D planeTexture, plane2Texture;
        private Texture2D projectileTexture, explosionTexture;
        private Texture2D pixel;

        /* --------------- Constantes rendu ---------- */
        private const float PlaneRenderSize      = 64f;
        private const float ProjectileRenderSize = 16f;
        private const float PlaneSpeed           = 200f;

        /* --------------- État runtime ------------- */
        private Dictionary<int, Vector2> projectiles = new();
        private readonly Dictionary<string, Vector2> playerPositions =
                new(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<string, int>     playerHealth =
                new(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<string, int>     prevHealth =
                new(StringComparer.OrdinalIgnoreCase);

        private bool   stateReceived = false;
        private bool   gameOver      = false;
        private string gameOverText  = string.Empty;

        private readonly ConcurrentQueue<string> queue = new();

        private KeyboardState prevKb = Keyboard.GetState();
        private bool isRightPlayer   = false;

        public MatchScreen(DogfightGame g, string me, NetworkClient n)
        {
            game = g;
            this.me = me;
            net = n;
        }

        /* ==================== LOAD ======================================= */
        public void LoadContent(ContentManager content)
        {
            font = content.Load<SpriteFont>("DefaultFont");

            string dir = Directory.GetCurrentDirectory();
            background        = LoadTexture("background.png",  dir);
            planeTexture      = LoadTexture("plane.png",       dir);
            plane2Texture     = LoadTexture("plane2.png",      dir);
            projectileTexture = LoadTexture("projectile.png",  dir);
            explosionTexture  = LoadTexture("explosion.png",   dir);

            pixel = new Texture2D(game.GraphicsDevice, 1, 1);
            pixel.SetData(new[] { Color.White });

            prevHealth.Clear();

            net.Subscribe(queue);          // écoute les paquets match
        }

        private Texture2D LoadTexture(string file, string baseDir)
        {
            string path = Path.Combine(baseDir, file);
            if (!File.Exists(path))
                throw new FileNotFoundException($"Missing texture: {path}");
            return Texture2D.FromStream(game.GraphicsDevice, File.OpenRead(path));
        }

        /* ==================== UPDATE ===================================== */
        public void Update(GameTime gameTime)
        {
            var kb = Keyboard.GetState();
            float dt = (float)gameTime.ElapsedGameTime.TotalSeconds;

            /* -------- Réception réseau -------- */
            while (queue.TryDequeue(out var raw))
            {
                var msg = raw.Trim();

                if (msg.StartsWith("STATE:", StringComparison.OrdinalIgnoreCase))
                    ParseState(msg["STATE:".Length..]);

                else if (msg.StartsWith("FIRE_ACK:", StringComparison.OrdinalIgnoreCase))
                    ParseFireAck(msg["FIRE_ACK:".Length..]);

                else if (msg.StartsWith("HIT:", StringComparison.OrdinalIgnoreCase))
                {
                    var p = msg.Split(':', StringSplitOptions.RemoveEmptyEntries);
                    if (p.Length == 3 && int.TryParse(p[2], out var hp))
                        playerHealth[p[1]] = hp;
                }
                else if (msg.StartsWith("GAME_OVER:", StringComparison.OrdinalIgnoreCase))
                {
                    var winner = msg.Split(':', 2)[1];
                    gameOver   = true;
                    gameOverText = string.Equals(winner, me, StringComparison.OrdinalIgnoreCase)
                                   ? "You win! Press Enter to return to lobby."
                                   : "You died! Press Enter to return to lobby.";
                }
            }

            if (!stateReceived) { prevKb = kb; return; }

            /* -------- Détection locale (secours) -------- */
            if (!gameOver &&
                playerHealth.TryGetValue(me, out var hpMe) &&
                hpMe <= 0)
            {
                gameOver   = true;
                gameOverText = "You died! Press Enter to return to lobby.";
            }
            else if (!gameOver)
            {
                foreach (var kv in playerHealth)
                    if (!string.Equals(kv.Key, me, StringComparison.OrdinalIgnoreCase) &&
                        kv.Value <= 0)
                    {
                        gameOver   = true;
                        gameOverText = "You win! Press Enter to return to lobby.";
                        break;
                    }
            }

            /* -------- Écran Game-Over -------- */
            if (gameOver)
            {
                if (IsNewKey(kb, prevKb, Keys.Enter))
                {
                    /* Reset local */
                    playerPositions.Clear();
                    playerHealth.Clear();
                    projectiles.Clear();
                    stateReceived = false;

                    net.ClearInbox();                 // évite les re-triggers
                    game.ChangeScreen(new LobbyScreen(game, me, net));
                }

                prevKb = kb;
                return;
            }

            /* -------- Contrôles avion -------- */
            Vector2 dir = Vector2.Zero;
            if (kb.IsKeyDown(Keys.Up))    dir.Y -= 1;
            if (kb.IsKeyDown(Keys.Down))  dir.Y += 1;
            if (kb.IsKeyDown(Keys.Left))  dir.X -= 1;
            if (kb.IsKeyDown(Keys.Right)) dir.X += 1;

            if (dir != Vector2.Zero &&
                playerPositions.TryGetValue(me, out var myPos))
            {
                dir.Normalize();
                myPos += dir * PlaneSpeed * dt;
                playerPositions[me] = myPos;
                net.SendLine($"MOVE:{myPos.X:F0}:{myPos.Y:F0}");
            }

            if (IsNewKey(kb, prevKb, Keys.Space) &&
                playerPositions.TryGetValue(me, out var pos))
            {
                float dx = isRightPlayer ? -10 : 10;
                net.SendLine($"FIRE:{pos.X:F0}:{pos.Y:F0}:{dx:F0}:0");
            }

            prevKb = kb;
        }

        /* ==================== DRAW ======================================= */
        public void Draw(SpriteBatch sb)
        {
            var vp = sb.GraphicsDevice.Viewport;
            sb.Draw(background, new Rectangle(0, 0, vp.Width, vp.Height), Color.White);

            /* projectiles */
            int pSize = (int)ProjectileRenderSize;
            foreach (var kv in projectiles)
            {
                var pos = kv.Value;
                sb.Draw(projectileTexture,
                        new Rectangle((int)(pos.X - pSize / 2),
                                      (int)(pos.Y - pSize / 2),
                                      pSize, pSize),
                        Color.White);
            }

            /* avions + HP */
            int size = (int)PlaneRenderSize;
            foreach (var kv in playerPositions)
            {
                var name = kv.Key;
                var pos  = kv.Value;

                bool isMe = string.Equals(name, me, StringComparison.OrdinalIgnoreCase);
                var  tex  = isMe
                            ? (isRightPlayer ? plane2Texture : planeTexture)
                            : (isRightPlayer ? planeTexture : plane2Texture);

                sb.Draw(tex,
                        new Rectangle((int)(pos.X - size / 2),
                                      (int)(pos.Y - size / 2),
                                      size, size),
                        Color.White);

                if (playerHealth.TryGetValue(name, out var hp))
                {
                    int barW = 50, barH = 6;
                    float bx = pos.X - barW / 2,
                          by = pos.Y - size / 2 - barH * 2;

                    sb.Draw(pixel, new Rectangle((int)bx, (int)by, barW, barH), Color.Red);
                    sb.Draw(pixel,
                            new Rectangle((int)bx, (int)by,
                                          (int)(barW * MathHelper.Clamp(hp / 100f, 0f, 1f)),
                                          barH),
                            Color.Green);

                    if (prevHealth.TryGetValue(name, out var prev) && prev > 0 && hp <= 0)
                        sb.Draw(explosionTexture,
                                new Rectangle((int)(pos.X - size / 2),
                                              (int)(pos.Y - size / 2),
                                              size, size),
                                Color.White);

                    prevHealth[name] = hp;
                }
            }

            if (gameOver)
            {
                var ts  = font.MeasureString(gameOverText);
                var box = new Rectangle((vp.Width  - (int)ts.X - 20) / 2,
                                        (vp.Height - (int)ts.Y - 20) / 2,
                                        (int)ts.X + 20,
                                        (int)ts.Y + 20);
                sb.Draw(pixel, box, new Color(0, 0, 0, 180));
                sb.DrawString(font, gameOverText,
                              new Vector2(box.X + 10, box.Y + 10), Color.White);
            }
        }

        /* ==================== PARSE & HELPERS ============================ */
        private void ParseState(string data)
        {
            stateReceived = true;
            var parts    = data.Split('|', 3);
            var projPart = parts[0];
            var hpPart   = parts.Length > 1 ? parts[1] : "";
            var posPart  = parts.Length > 2 ? parts[2] : "";

            /* projectiles */
            var newProj = new Dictionary<int, Vector2>();
            if (!string.IsNullOrEmpty(projPart))
                foreach (var e in projPart.Split(',', StringSplitOptions.RemoveEmptyEntries))
                {
                    var v = e.Split(':');
                    if (v.Length == 3 &&
                        int.TryParse(v[0], out var id) &&
                        float.TryParse(v[1], out var x) &&
                        float.TryParse(v[2], out var y))
                        newProj[id] = new Vector2(x, y);
                }
            projectiles = newProj;

            /* HP */
            var newHP = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
            if (!string.IsNullOrEmpty(hpPart))
                foreach (var e in hpPart.Split(',', StringSplitOptions.RemoveEmptyEntries))
                {
                    var kv = e.Split(':');
                    if (kv.Length == 2 && int.TryParse(kv[1], out var hp))
                        newHP[kv[0]] = hp;
                }
            playerHealth.Clear();
            foreach (var kv in newHP) playerHealth[kv.Key] = kv.Value;

            /* positions */
            if (!string.IsNullOrEmpty(posPart))
                foreach (var e in posPart.Split(',', StringSplitOptions.RemoveEmptyEntries))
                {
                    var kv = e.Split(':');
                    if (kv.Length == 3 &&
                        float.TryParse(kv[1], out var x) &&
                        float.TryParse(kv[2], out var y))
                        playerPositions[kv[0]] = new Vector2(x, y);
                }

            if (playerPositions.TryGetValue(me, out var myPos))
                isRightPlayer = myPos.X > 280;
        }

        private void ParseFireAck(string data)
        {
            var p = data.Split(':');
            if (p.Length >= 4 &&
                int.TryParse(p[0], out var id) &&
                float.TryParse(p[2], out var x) &&
                float.TryParse(p[3], out var y))
            {
                projectiles[id] = new Vector2(x, y);
            }
        }

        private static bool IsNewKey(KeyboardState cur, KeyboardState prev, Keys k)
            => cur.IsKeyDown(k) && !prev.IsKeyDown(k);
    }
}
