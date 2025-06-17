using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Content;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;

namespace ClientGame
{
    /// <summary>Lobby : liste des joueurs, chat, logout &amp; delete-account.</summary>
    public sealed class LobbyScreen : IScreen
    {
        /* ─────────── Références ─────────── */
        private readonly DogfightGame  game;
        private readonly NetworkClient net;
        private readonly string        me;

        /* ─────────── Ressources ─────────── */
        private SpriteFont font;
        private Texture2D  pixel;

        /* ─────────── États ─────────── */
        private readonly List<string> players = new();
        private readonly List<string> chatLog = new();
        private readonly ConcurrentQueue<string> queue = new();

        private int  selectedIndex = 0;
        private bool inviteLatch   = false;
        private bool showingPrompt = false;   /* popup d’invitation */
        private string inviter     = "";

        /* Chat */
        private const int ChatW = 240, ChatH = 380, ChatPad = 8;
        private bool   typingChat = false;
        private string chatInput  = "";

        /* Logout / delete */
        private bool askDelConfirm = false;
        private bool pendingLogout = false;   /* on attend LOGOUT_OK */

        /* Résultats requêtes SQL */
        private string queryResult = "";

        /* Boutons */
        private Rectangle btnQ1, btnQ2, btnQ3, btnLogout, btnDelete;

        /* Entrées précédentes */
        private KeyboardState prevKb = Keyboard.GetState();
        private MouseState    prevMs = Mouse.GetState();

        public LobbyScreen(DogfightGame g, string me, NetworkClient n)
        { game = g; this.me = me; net = n; }

        /* ═════════════════════ LOAD ═════════════════════ */
        public void LoadContent(ContentManager content)
        {
            font  = content.Load<SpriteFont>("DefaultFont");
            pixel = NewPixel(game.GraphicsDevice);

            btnQ1 = MakeBtn("Query1", new Vector2( 50, 380));
            btnQ2 = MakeBtn("Query2", new Vector2(200, 380));
            btnQ3 = MakeBtn("Query3", new Vector2(350, 380));

            var vp = game.GraphicsDevice.Viewport;
            btnLogout = MakeBtn("Logout", new Vector2(vp.Width - 220, vp.Height - 60));
            btnDelete = MakeBtn("Delete", new Vector2(vp.Width - 110, vp.Height - 60));

            /* vide la boîte centrale */
            while (net.Inbox.TryTake(out var l)) queue.Enqueue(l);
            net.Subscribe(queue);
            net.SendLine("LIST");
        }

        /* ═════════════════════ UPDATE ═════════════════════ */
        public void Update(GameTime _)
        {
            var kb = Keyboard.GetState();
            var ms = Mouse.GetState();

            /* ─── 1) Réception réseau ─── */
            while (queue.TryDequeue(out var raw))
            {
                var msg = raw.Trim();

                /* ► accusé logout */
                if (msg.Equals("LOGOUT_OK", StringComparison.OrdinalIgnoreCase))
                {
                    net.Dispose();
                    game.ChangeScreen(new LoginScreen(game));
                    return;
                }

                /* ► mises à jour classiques */
                if (msg.StartsWith("UPDATE_LIST:", StringComparison.OrdinalIgnoreCase))
                {
                    players.Clear();
                    players.AddRange(msg["UPDATE_LIST:".Length..]
                                     .Split(',', StringSplitOptions.RemoveEmptyEntries));
                    if (selectedIndex >= players.Count) selectedIndex = players.Count - 1;
                }
                else if (msg.StartsWith("CHAT:", StringComparison.OrdinalIgnoreCase))
                {
                    var p = msg.Split(':', 3);
                    if (p.Length == 3) chatLog.Add($"{p[1]}: {p[2]}");
                }
                else if (msg.StartsWith("INVITE_REQUEST:", StringComparison.OrdinalIgnoreCase))
                {
                    var pay = msg["INVITE_REQUEST:".Length..];
                    var prt = pay.Split(':', 2);
                    var tgt = prt[1].Split(',', StringSplitOptions.RemoveEmptyEntries);
                    if (Array.Exists(tgt, x => x.Equals(me, StringComparison.OrdinalIgnoreCase)))
                    { inviter = prt[0]; showingPrompt = true; }
                }
                else if (msg.StartsWith("INVITE_RESULT:", StringComparison.OrdinalIgnoreCase))
                {
                    var p = msg.Split(':', 3);
                    if (p.Length == 3 && p[2].Equals("ACCEPTED", StringComparison.OrdinalIgnoreCase))
                    {
                        game.ChangeScreen(new MatchScreen(game, me, net));
                        prevKb = kb; prevMs = ms;
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

            /* ─── 2) si on attend juste le LOGOUT_OK, on ne fait plus rien ─── */
            if (pendingLogout) { prevKb = kb; prevMs = ms; return; }

            /* ─── 3) Confirmation delete ─── */
            if (askDelConfirm)
            {
                if (IsNewKey(kb, prevKb, Keys.Y))
                {
                    net.SendLine("DELETE_ME");
                    pendingLogout = true;          // le serveur fermera après suppression
                }
                else if (IsNewKey(kb, prevKb, Keys.N) || IsNewKey(kb, prevKb, Keys.Escape))
                    askDelConfirm = false;

                prevKb = kb; prevMs = ms;
                return;
            }

            /* ─── 4) Saisie chat ─── */
            if (typingChat) { HandleChatInput(kb); prevKb = kb; prevMs = ms; return; }

            /* ─── 5) Activation champ chat ─── */
            var vp   = game.GraphicsDevice.Viewport;
            var chatRect = new Rectangle(vp.Width - ChatW - 20, 20, ChatW, ChatH);
            bool clickChat = ms.LeftButton == ButtonState.Pressed &&
                             prevMs.LeftButton == ButtonState.Released &&
                             chatRect.Contains(ms.Position);
            if (IsNewKey(kb, prevKb, Keys.T) || clickChat)
            { typingChat = true; chatInput = ""; }

            /* ─── 6) Liste joueurs ─── */
            if (IsNewKey(kb, prevKb, Keys.Down) && players.Count > 0)
                selectedIndex = (selectedIndex + 1) % players.Count;
            if (IsNewKey(kb, prevKb, Keys.Up) && players.Count > 0)
                selectedIndex = (selectedIndex - 1 + players.Count) % players.Count;

            /* ─── 7) Invitation ─── */
            if (!inviteLatch && IsNewKey(kb, prevKb, Keys.Enter) && players.Count > 0)
            {
                var target = players[selectedIndex];
                if (!target.Equals(me, StringComparison.OrdinalIgnoreCase))
                    net.SendLine($"INVITE:{target}");
                inviteLatch = true;
            }
            if (kb.IsKeyUp(Keys.Enter)) inviteLatch = false;

            /* ─── 8) Boutons ─── */
            if (ms.LeftButton == ButtonState.Pressed && prevMs.LeftButton == ButtonState.Released)
            {
                if (btnQ1.Contains(ms.Position)) { net.SendLine("QUERY1"); queryResult = ""; }
                if (btnQ2.Contains(ms.Position)) { net.SendLine("QUERY2"); queryResult = ""; }
                if (btnQ3.Contains(ms.Position)) { net.SendLine("QUERY3"); queryResult = ""; }

                if (btnLogout.Contains(ms.Position))
                {
                    net.SendLine("LOGOUT");
                    pendingLogout = true;
                }
                else if (btnDelete.Contains(ms.Position))
                    askDelConfirm = true;
            }

            /* ─── 9) Raccourcis clavier ─── */
            if (IsNewKey(kb, prevKb, Keys.Escape))
            {
                net.SendLine("LOGOUT");
                pendingLogout = true;
            }
            if (IsNewKey(kb, prevKb, Keys.Delete)) askDelConfirm = true;

            /* ─── 10) Popup invitation ─── */
            if (showingPrompt)
            {
                if (IsNewKey(kb, prevKb, Keys.Y))
                { net.SendLine($"INVITE_RESP:{inviter}:ACCEPT"); showingPrompt = false; }
                else if (IsNewKey(kb, prevKb, Keys.N))
                { net.SendLine($"INVITE_RESP:{inviter}:REJECT"); showingPrompt = false; }
            }

            prevKb = kb; prevMs = ms;
        }

        /* ═════════════════════ DRAW ═════════════════════ */
        public void Draw(SpriteBatch sb)
        {
            sb.GraphicsDevice.Clear(Color.CornflowerBlue);
            EnsurePixel(sb);

            sb.DrawString(font, $"Lobby - You: {me}", new Vector2(50, 30), Color.White);

            for (int i = 0; i < players.Count; i++)
            {
                var col = i == selectedIndex ? Color.Yellow : Color.White;
                sb.DrawString(font, players[i], new Vector2(60, 80 + i * 30), col);
            }

            sb.DrawString(font, "Up/Down to move   Enter to invite",
                          new Vector2(50, 330), Color.LightGray);

            DrawBtn(sb, btnQ1, "Query1");
            DrawBtn(sb, btnQ2, "Query2");
            DrawBtn(sb, btnQ3, "Query3");
            DrawBtn(sb, btnLogout, "Logout");
            DrawBtn(sb, btnDelete, "Delete");

            /* ► chat pane */
            var vp = sb.GraphicsDevice.Viewport;
            var pane = new Rectangle(vp.Width - ChatW - 20, 20, ChatW, ChatH);
            sb.Draw(pixel, pane, new Color(0, 0, 0, 170));
            DrawBorder(sb, pane, 2, Color.White);

            /* historique */
            float y = pane.Y + ChatPad;
            foreach (var line in GetWrappedChatLines())
            {
                sb.DrawString(font, line, new Vector2(pane.X + ChatPad, y), Color.White);
                y += font.LineSpacing;
            }

            /* input */
            var inputPos = new Vector2(pane.X + ChatPad,
                                       pane.Bottom - font.LineSpacing - ChatPad);
            if (typingChat)
                sb.DrawString(font, $"> {chatInput}_", inputPos, Color.Yellow);
            else
                sb.DrawString(font, "(T or click to chat)", inputPos, Color.Gray);

            /* pop-ups */
            if (!string.IsNullOrEmpty(queryResult)) DrawPopup(sb, queryResult);
            if (showingPrompt) DrawPopup(sb, $"{inviter} invites you.  Y / N");
            if (askDelConfirm) DrawPopup(sb, "Delete account?  Y / N");
        }

        /* ─────────── Helpers graphiques ─────────── */
        private Texture2D NewPixel(GraphicsDevice gd)
        { var t = new Texture2D(gd, 1, 1); t.SetData(new[] { Color.White }); return t; }

        private void EnsurePixel(SpriteBatch sb)
        { if (pixel == null || pixel.IsDisposed) pixel = NewPixel(sb.GraphicsDevice); }

        private Rectangle MakeBtn(string txt, Vector2 pos)
        { var s = font.MeasureString(txt); return new Rectangle((int)pos.X, (int)pos.Y, (int)s.X + 20, (int)s.Y + 10); }

        private void DrawBtn(SpriteBatch sb, Rectangle r, string txt)
        {
            sb.Draw(pixel, r, Color.DimGray);
            var col = r.Contains(Mouse.GetState().Position) ? Color.Yellow : Color.White;
            sb.DrawString(font, txt, new Vector2(r.X + 10, r.Y + 5), col);
        }

        private void DrawBorder(SpriteBatch sb, Rectangle r, int t, Color c)
        {
            sb.Draw(pixel, new Rectangle(r.X, r.Y, r.Width, t), c);
            sb.Draw(pixel, new Rectangle(r.X, r.Y + r.Height - t, r.Width, t), c);
            sb.Draw(pixel, new Rectangle(r.X, r.Y, t, r.Height), c);
            sb.Draw(pixel, new Rectangle(r.X + r.Width - t, r.Y, t, r.Height), c);
        }

        private void DrawPopup(SpriteBatch sb, string txt)
        {
            var vp = sb.GraphicsDevice.Viewport;
            var lines = Wrap(txt, vp.Width * 0.6f);
            float maxW = 0; foreach (var l in lines)
                maxW = Math.Max(maxW, font.MeasureString(l).X);

            var pad = new Vector2(20, 20);
            var size = new Vector2(maxW, font.LineSpacing * lines.Count) + pad * 2;
            var pos  = new Vector2((vp.Width - size.X) / 2, (vp.Height - size.Y) / 2);
            var rect = new Rectangle((int)pos.X, (int)pos.Y, (int)size.X, (int)size.Y);

            sb.Draw(pixel, rect, new Color(0, 0, 0, 190));
            DrawBorder(sb, rect, 3, Color.White);

            for (int i = 0; i < lines.Count; i++)
                sb.DrawString(font, lines[i], pos + pad + new Vector2(0, i * font.LineSpacing), Color.White);
        }

        /* ─────────── Chat utils ─────────── */
        private void HandleChatInput(KeyboardState kb)
        {
            foreach (var k in kb.GetPressedKeys())
            {
                if (prevKb.IsKeyDown(k)) continue;

                if (k == Keys.Enter)
                {
                    if (chatInput.Length > 0) net.SendLine($"CHAT:{me}:{chatInput}");
                    chatInput = ""; typingChat = false; return;
                }
                if (k == Keys.Escape) { chatInput = ""; typingChat = false; return; }
                if (k == Keys.Back && chatInput.Length > 0) { chatInput = chatInput[..^1]; continue; }

                bool shift = kb.IsKeyDown(Keys.LeftShift) || kb.IsKeyDown(Keys.RightShift);
                char ch = KeyToChar(k, shift);
                if (ch != '\0') chatInput += ch;
            }
        }

        private IEnumerable<string> GetWrappedChatLines()
        {
            var lines = new List<string>();
            float max = ChatW - ChatPad * 2;
            for (int i = chatLog.Count - 1; i >= 0; i--)
            {
                var wrap = Wrap(chatLog[i], max);
                for (int j = wrap.Count - 1; j >= 0; j--) lines.Insert(0, wrap[j]);
                if (lines.Count * font.LineSpacing > ChatH - font.LineSpacing * 2) break;
            }
            return lines;
        }

        private List<string> Wrap(string txt, float maxW)
        {
            var words = txt.Split(' ');
            var res = new List<string>();
            string cur = "";

            foreach (var w in words)
            {
                var test = string.IsNullOrEmpty(cur) ? w : $"{cur} {w}";
                if (font.MeasureString(test).X > maxW)
                { if (cur.Length > 0) res.Add(cur); cur = w; }
                else cur = test;
            }
            if (cur.Length > 0) res.Add(cur);
            return res;
        }

        private static char KeyToChar(Keys k, bool shift)
        {
            if (k >= Keys.A && k <= Keys.Z)
            { char c = (char)('a' + (k - Keys.A)); return shift ? char.ToUpper(c) : c; }
            if (k >= Keys.D0 && k <= Keys.D9) return (char)('0' + (k - Keys.D0));
            return k switch
            {
                Keys.Space     => ' ',
                Keys.OemComma  => ',',
                Keys.OemPeriod => '.',
                Keys.OemMinus  => shift ? '_' : '-',
                Keys.OemPlus   => shift ? '+' : '=',
                _              => '\0'
            };
        }

        private static bool IsNewKey(KeyboardState cur, KeyboardState prev, Keys k)
            => cur.IsKeyDown(k) && !prev.IsKeyDown(k);
    }
}
