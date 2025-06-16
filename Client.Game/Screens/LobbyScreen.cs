using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Content;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;

namespace ClientGame
{
    public class LobbyScreen : IScreen
    {
        private readonly DogfightGame  game;
        private readonly NetworkClient net;
        private readonly string        me;

        /* ---------- UI / état ---------- */
        private SpriteFont  font;
        private Texture2D   pixel;                 // texture blanche 1×1
        private List<string> players = new();
        private int         selectedIndex = 0;

        private KeyboardState prevKb   = Keyboard.GetState();
        private MouseState    prevMouse = Mouse.GetState();
        private bool inviteLatch = false;

        private Rectangle btnQuery1, btnQuery2, btnQuery3;

        private bool   showingPrompt = false;
        private string inviter       = "";

        private string queryResult   = string.Empty;

        /* ---------- Réception réseau ---------- */
        private readonly ConcurrentQueue<string> queue = new();

        public LobbyScreen(DogfightGame g, string me, NetworkClient n)
        {
            game = g;
            this.me = me;
            net = n;
        }

        /* =========================================================
                               LOAD
        =========================================================*/
        public void LoadContent(ContentManager content)
        {
            font = content.Load<SpriteFont>("DefaultFont");
            CreatePixel();                              // première création

            btnQuery1 = MakeButtonRect("Query1", new Vector2(50, 380));
            btnQuery2 = MakeButtonRect("Query2", new Vector2(200, 380));
            btnQuery3 = MakeButtonRect("Query3", new Vector2(350, 380));

            /* vide la boîte centrale pour ne rien rater */
            while (net.Inbox.TryTake(out var line))
                queue.Enqueue(line);

            net.Subscribe(queue);
            net.SendLine("LIST");          // demande immédiate de la liste
        }

        /* ---------------------------------------------------------
                            Helpers init
        ---------------------------------------------------------*/
        private void CreatePixel()                         // crée la 1×1
        {
            pixel = new Texture2D(game.GraphicsDevice, 1, 1);
            pixel.SetData(new[] { Color.White });
        }

        private void EnsurePixelAlive(SpriteBatch sb)      // recrée si null/disposed
        {
            if (pixel == null || pixel.IsDisposed)
            {
                pixel = new Texture2D(sb.GraphicsDevice, 1, 1);
                pixel.SetData(new[] { Color.White });
            }
        }

        private Rectangle MakeButtonRect(string text, Vector2 pos)
        {
            var sz = font.MeasureString(text);
            return new Rectangle((int)pos.X, (int)pos.Y,
                                 (int)sz.X + 20, (int)sz.Y + 10);
        }

        /* =========================================================
                               UPDATE
        =========================================================*/
        public void Update(GameTime _)
        {
            var kb    = Keyboard.GetState();
            var mouse = Mouse.GetState();

            /* lecture messages réseau (inchangé)… */
            while (queue.TryDequeue(out var raw))
            {
                var msg = raw.Trim();
                if (msg.StartsWith("UPDATE_LIST:", StringComparison.OrdinalIgnoreCase))
                {
                    var list = msg["UPDATE_LIST:".Length..]
                               .Split(',', StringSplitOptions.RemoveEmptyEntries);
                    players = new List<string>(list);
                    if (selectedIndex >= players.Count)
                        selectedIndex = players.Count - 1;
                }
                else if (msg.StartsWith("INVITE_REQUEST:", StringComparison.OrdinalIgnoreCase))
                {
                    var payload = msg["INVITE_REQUEST:".Length..];
                    var parts   = payload.Split(':', 2);
                    var targets = parts[1].Split(',', StringSplitOptions.RemoveEmptyEntries);

                    if (Array.Exists(targets,
                          x => string.Equals(x, me, StringComparison.OrdinalIgnoreCase)))
                    {
                        inviter       = parts[0];
                        showingPrompt = true;
                    }
                }
                else if (msg.StartsWith("INVITE_RESULT:", StringComparison.OrdinalIgnoreCase))
                {
                    var parts = msg.Split(':', 3);
                    if (parts.Length == 3 &&
                        parts[2].Equals("ACCEPTED", StringComparison.OrdinalIgnoreCase))
                    {
                        game.ChangeScreen(new MatchScreen(game, me, net));
                        prevKb    = kb;
                        prevMouse = mouse;
                        return;
                    }
                }
                else if (msg.StartsWith("QUERY1_RESULT:", StringComparison.OrdinalIgnoreCase))
                    queryResult = msg["QUERY1_RESULT:".Length..];
                else if (msg.StartsWith("QUERY2_RESULT:", StringComparison.OrdinalIgnoreCase))
                    queryResult = msg["QUERY2_RESULT:".Length..];
                else if (msg.StartsWith("QUERY3_RESULT:", StringComparison.OrdinalIgnoreCase))
                    queryResult = msg["QUERY3_RESULT:".Length..];
            }

            /* popup, navigation, invitation, boutons — inchangés */
            /* … */
            if (showingPrompt)
            {
                if (IsNewKey(kb, prevKb, Keys.Y))
                {
                    net.SendLine($"INVITE_RESP:{inviter}:ACCEPT");
                    showingPrompt = false;
                }
                else if (IsNewKey(kb, prevKb, Keys.N))
                {
                    net.SendLine($"INVITE_RESP:{inviter}:REJECT");
                    showingPrompt = false;
                }
                prevKb    = kb;
                prevMouse = mouse;
                return;
            }

            if (IsNewKey(kb, prevKb, Keys.Down) && players.Count > 0)
                selectedIndex = (selectedIndex + 1) % players.Count;
            if (IsNewKey(kb, prevKb, Keys.Up) && players.Count > 0)
                selectedIndex = (selectedIndex - 1 + players.Count) % players.Count;

            if (!inviteLatch &&
                IsNewKey(kb, prevKb, Keys.Enter) &&
                players.Count > 0)
            {
                var target = players[selectedIndex];
                if (!string.Equals(target, me, StringComparison.OrdinalIgnoreCase))
                    net.SendLine($"INVITE:{target}");
                inviteLatch = true;
            }
            if (kb.IsKeyUp(Keys.Enter)) inviteLatch = false;

            if (mouse.LeftButton == ButtonState.Pressed &&
                prevMouse.LeftButton == ButtonState.Released)
            {
                if (btnQuery1.Contains(mouse.Position)) { net.SendLine("QUERY1"); queryResult = string.Empty; }
                if (btnQuery2.Contains(mouse.Position)) { net.SendLine("QUERY2"); queryResult = string.Empty; }
                if (btnQuery3.Contains(mouse.Position)) { net.SendLine("QUERY3"); queryResult = string.Empty; }
            }

            prevKb    = kb;
            prevMouse = mouse;
        }

        /* =========================================================
                                DRAW
        =========================================================*/
        public void Draw(SpriteBatch sb)
        {
            EnsurePixelAlive(sb);                  // toujours avant tout rendu
            sb.GraphicsDevice.Clear(Color.CornflowerBlue);

            sb.DrawString(font, $"Lobby - You: {me}",
                          new Vector2(50, 30), Color.White);

            for (int i = 0; i < players.Count; i++)
            {
                var col = i == selectedIndex ? Color.Yellow : Color.White;
                sb.DrawString(font, players[i],
                              new Vector2(60, 80 + i * 30), col);
            }

            sb.DrawString(font, "Up/Down to move   Enter to invite",
                          new Vector2(50, 350), Color.LightGray);

            DrawButton(sb, btnQuery1, "Query1");
            DrawButton(sb, btnQuery2, "Query2");
            DrawButton(sb, btnQuery3, "Query3");

            if (!string.IsNullOrEmpty(queryResult))
                DrawPopup(sb, queryResult);

            if (showingPrompt)
                DrawPopup(sb, $"{inviter} invites you. Press Y to accept or N to reject.");
        }

        /* =========================================================
                           Helper rendering
        =========================================================*/
        private void DrawPopup(SpriteBatch sb, string text)
        {
            EnsurePixelAlive(sb);

            var vp         = sb.GraphicsDevice.Viewport;
            float maxWidth = vp.Width * 0.6f;
            float lineH    = font.LineSpacing;

            var lines = WrapText(text, font, maxWidth);
            float textW = 0f;
            foreach (var l in lines)
                textW = Math.Max(textW, font.MeasureString(l).X);

            var padding   = new Vector2(20, 20);
            var popupSize = new Vector2(textW, lineH * lines.Count) + padding * 2;
            var popupPos  = new Vector2((vp.Width  - popupSize.X) / 2,
                                        (vp.Height - popupSize.Y) / 2);

            var rect = new Rectangle((int)popupPos.X, (int)popupPos.Y,
                                     (int)popupSize.X, (int)popupSize.Y);

            sb.Draw(pixel, rect, new Color(0, 0, 0, 180));
            DrawBorder(sb, rect, 3, Color.White);

            for (int i = 0; i < lines.Count; i++)
            {
                var pos = popupPos + padding + new Vector2(0, i * lineH);
                sb.DrawString(font, lines[i], pos, Color.White);
            }
        }

        private void DrawButton(SpriteBatch sb, Rectangle rect, string text)
        {
            EnsurePixelAlive(sb);
            sb.Draw(pixel, rect, Color.DarkGray);

            var mouse = Mouse.GetState();
            var col   = rect.Contains(mouse.Position) ? Color.Yellow : Color.White;

            sb.DrawString(font, text,
                          new Vector2(rect.X + 10, rect.Y + 5), col);
        }

        private void DrawBorder(SpriteBatch sb, Rectangle r, int t, Color c)
        {
            EnsurePixelAlive(sb);
            sb.Draw(pixel, new Rectangle(r.X, r.Y, r.Width, t), c);
            sb.Draw(pixel, new Rectangle(r.X, r.Y + r.Height - t, r.Width, t), c);
            sb.Draw(pixel, new Rectangle(r.X, r.Y, t, r.Height), c);
            sb.Draw(pixel, new Rectangle(r.X + r.Width - t, r.Y, t, r.Height), c);
        }

        /* ========================================================= */
        private static bool IsNewKey(KeyboardState cur, KeyboardState prev, Keys k)
            => cur.IsKeyDown(k) && !prev.IsKeyDown(k);

        private List<string> WrapText(string text, SpriteFont f, float maxLineW)
        {
            var words = text.Split(' ');
            var lines = new List<string>();
            var cur   = "";

            foreach (var w in words)
            {
                var test = string.IsNullOrEmpty(cur) ? w : cur + " " + w;
                if (f.MeasureString(test).X > maxLineW)
                {
                    if (!string.IsNullOrEmpty(cur)) lines.Add(cur);
                    cur = w;
                }
                else cur = test;
            }
            if (!string.IsNullOrEmpty(cur)) lines.Add(cur);
            return lines;
        }
    }
}
