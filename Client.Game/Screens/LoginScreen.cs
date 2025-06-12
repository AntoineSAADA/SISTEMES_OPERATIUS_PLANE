using System;
using System.IO;
using System.Threading.Tasks;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Content;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;

namespace ClientGame
{
    public class LoginScreen : IScreen
    {
        /* ⇢ Adresse du serveur – change-la ici si besoin */
        private const string ServerIP = "192.168.1.63";
        private const int    ServerPort = 12345;

        private readonly DogfightGame game;

        private SpriteFont font;
        private Texture2D  background;
        private NetworkClient net;

        /* ---------- État UI ---------- */
        private bool   isRegister = false;
        private string username   = "";
        private string email      = "";
        private string password   = "";
        private string status     = "Enter credentials, Tab to switch field";

        private bool enterLatch = false;
        private int  fieldIndex = 0;

        /* ---------- Imput antérieur ---------- */
        private KeyboardState prevKb   = Keyboard.GetState();
        private MouseState    prevMouse = Mouse.GetState();

        /* ---------- Rectangles pour les onglets ---------- */
        private Rectangle tabLoginRect;
        private Rectangle tabRegisterRect;

        public LoginScreen(DogfightGame g) => game = g;

        /* ====================================================================
           ================  Chargement des ressources  ========================
           ===================================================================*/
        public void LoadContent(ContentManager content)
        {
            font = content.Load<SpriteFont>("DefaultFont");

            string baseDir = Directory.GetCurrentDirectory();
            string path    = Path.Combine(baseDir, "backgroundhall.png");
            if (!File.Exists(path))
                throw new FileNotFoundException($"❌ Image de fond manquante : {path}");

            background = Texture2D.FromStream(game.GraphicsDevice, File.OpenRead(path));

            var loginSize = font.MeasureString("Login");
            var regSize   = font.MeasureString("Register");

            tabLoginRect    = new Rectangle( 80, 10, (int)loginSize.X + 20,  (int)loginSize.Y + 10);
            tabRegisterRect = new Rectangle(180, 10, (int)regSize.X  + 20,  (int)regSize.Y  + 10);
        }

        /* ====================================================================
                                  BOUCLE UPDATE
           ===================================================================*/
        public void Update(GameTime gameTime)
        {
            var kb    = Keyboard.GetState();
            var mouse = Mouse.GetState();

            /* ---------- Clic sur onglets ---------- */
            if (mouse.LeftButton == ButtonState.Pressed &&
                prevMouse.LeftButton == ButtonState.Released)
            {
                if (tabLoginRect.Contains(mouse.Position))    isRegister = false;
                if (tabRegisterRect.Contains(mouse.Position)) isRegister = true;
            }

            /* ---------- Tab : changer de champ ---------- */
            if (IsNewKey(kb, prevKb, Keys.Tab))
            {
                int maxField = isRegister ? 2 : 1;
                fieldIndex = (fieldIndex + 1) % (maxField + 1);
            }

            /* ---------- Backspace ---------- */
            if (IsNewKey(kb, prevKb, Keys.Back))
            {
                switch (fieldIndex)
                {
                    case 0: if (username.Length  > 0) username  = username[..^1];  break;
                    case 1:
                        if (isRegister)
                            email = email.Length > 0 ? email[..^1] : email;
                        else
                            password = password.Length > 0 ? password[..^1] : password;
                        break;
                    case 2: if (password.Length > 0) password = password[..^1];   break;
                }
            }

            /* ---------- Saisie alpha-num ---------- */
            foreach (var key in kb.GetPressedKeys())
            {
                if (!IsNewKey(kb, prevKb, key)) continue;

                char c = '\0';
                if (key is >= Keys.A and <= Keys.Z)   c = (char)('a' + (key - Keys.A));
                else if (key is >= Keys.D0 and <= Keys.D9) c = (char)('0' + (key - Keys.D0));
                else if (key == Keys.Space)           c = ' ';

                if (c == '\0') continue;
                if (kb.IsKeyDown(Keys.LeftShift) || kb.IsKeyDown(Keys.RightShift))
                    c = char.ToUpper(c);

                switch (fieldIndex)
                {
                    case 0: username += c;                break;
                    case 1: if (isRegister) email += c;   else password += c; break;
                    case 2: password += c;                break;
                }
            }

            /* ---------- Entrée : Login/Register ---------- */
            if (!enterLatch && IsNewKey(kb, prevKb, Keys.Enter))
            {
                enterLatch = true;
                _ = isRegister ? TryRegisterAsync() : TryLoginAsync();
            }
            if (kb.IsKeyUp(Keys.Enter)) enterLatch = false;

            prevKb   = kb;
            prevMouse = mouse;
        }

        /* ====================================================================
                                  LOGIN  /  REGISTER
           ===================================================================*/
        private async Task TryLoginAsync()
        {
            if (string.IsNullOrWhiteSpace(username) ||
                string.IsNullOrWhiteSpace(password))
            {
                status = "Username & password required.";
                return;
            }

            status = "Connecting...";
            net?.Dispose();
            net = new NetworkClient();

            if (!await net.ConnectAsync(ServerIP, ServerPort))
            {
                status = "Cannot reach server.";
                return;
            }

            net.SendLine($"LOGIN:{username}:{password}");

            if (!net.Inbox.TryTake(out var response, TimeSpan.FromSeconds(5)))
            {
                status = "Server not responding.";
                return;
            }

            response = response.Trim();
            if (response.Equals("Login successful", StringComparison.OrdinalIgnoreCase))
            {
                game.ChangeScreen(new LobbyScreen(game, username, net));
            }
            else
            {
                status = response;
            }
        }

        private async Task TryRegisterAsync()
        {
            if (string.IsNullOrWhiteSpace(username) ||
                string.IsNullOrWhiteSpace(email)    ||
                string.IsNullOrWhiteSpace(password))
            {
                status = "All fields required.";
                return;
            }

            status = "Connecting...";
            net?.Dispose();
            net = new NetworkClient();

            if (!await net.ConnectAsync(ServerIP, ServerPort))
            {
                status = "Cannot reach server.";
                return;
            }

            net.SendLine($"REGISTER:{username}:{email}:{password}");

            if (!net.Inbox.TryTake(out var response, TimeSpan.FromSeconds(5)))
            {
                status = "Server not responding.";
                return;
            }

            response = response.Trim();
            status   = response;

            if (response.StartsWith("Registration successful", StringComparison.OrdinalIgnoreCase))
            {
                isRegister = false;
                password   = "";
                status     = "Registered! Please login.";
            }
        }

        /* ====================================================================
                                    DRAW
           ===================================================================*/
        public void Draw(SpriteBatch sb)
        {
            sb.GraphicsDevice.Clear(Color.CornflowerBlue);
            sb.Draw(background,
                    sb.GraphicsDevice.Viewport.Bounds, Color.White);

            sb.DrawString(font, "Login",
                new Vector2(tabLoginRect.X + 10, tabLoginRect.Y + 5),
                isRegister ? Color.Gray : Color.White);

            sb.DrawString(font, "Register",
                new Vector2(tabRegisterRect.X + 10, tabRegisterRect.Y + 5),
                isRegister ? Color.White : Color.Gray);

            float underlineY = tabLoginRect.Y + tabLoginRect.Height;
            sb.DrawString(font, "_", new Vector2(
                (isRegister ? tabRegisterRect.X : tabLoginRect.X) + 10,
                underlineY), Color.Yellow);

            float baseY = 80;

            sb.DrawString(font, "Username:", new Vector2(80, baseY), Color.White);
            sb.DrawString(font, fieldIndex == 0 ? username + "|" : username,
                new Vector2(200, baseY), Color.Yellow);

            if (isRegister)
            {
                sb.DrawString(font, "Email:", new Vector2(80, baseY + 40), Color.White);
                sb.DrawString(font, fieldIndex == 1 ? email + "|" : email,
                    new Vector2(200, baseY + 40), Color.Yellow);

                sb.DrawString(font, "Password:", new Vector2(80, baseY + 80), Color.White);
                sb.DrawString(font,
                    fieldIndex == 2 ? password + "|" : new string('*', password.Length),
                    new Vector2(200, baseY + 80), Color.Yellow);
            }
            else
            {
                sb.DrawString(font, "Password:", new Vector2(80, baseY + 40), Color.White);
                sb.DrawString(font,
                    fieldIndex == 1 ? password + "|" : new string('*', password.Length),
                    new Vector2(200, baseY + 40), Color.Yellow);
            }

            sb.DrawString(font, status,
                new Vector2(80, baseY + (isRegister ? 120 : 80)), Color.LightGray);

            sb.DrawString(font, "[Tab] switch field",
                new Vector2(80, baseY + (isRegister ? 160 : 120)), Color.Gray);

            sb.DrawString(font, "[Enter] submit",
                new Vector2(300, baseY + (isRegister ? 160 : 120)), Color.Gray);
        }

        private static bool IsNewKey(KeyboardState cur, KeyboardState prev, Keys k)
            => cur.IsKeyDown(k) && !prev.IsKeyDown(k);
    }
}
