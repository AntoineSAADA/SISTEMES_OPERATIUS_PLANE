using System;
using System.Drawing;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks; // Pour utiliser Task et async/await
using System.Windows.Forms;

namespace ClientApp
{
    public class LoginForm : Form
    {
        private TextBox txtEmail, txtPassword;
        private Button btnLogin;
        private LinkLabel linkRegister;  // Pour créer un compte
        private const string SERVER_IP = "127.0.0.1";
        private const int SERVER_PORT = 8080;

        public LoginForm()
        {
            this.Text = "Login";
            this.Size = new Size(400, 250);
            this.StartPosition = FormStartPosition.CenterScreen;
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;

            // Mettre le fond en bleu ciel 
            this.BackColor = Color.SkyBlue;
            this.ForeColor = Color.Black;

            InitializeComponents();
        }

        private void InitializeComponents()
        {
            Label lblTitle = new Label
            {
                Text = "Please log in",
                Location = new Point(140, 20),
                AutoSize = true,
                Font = new Font("Segoe UI", 12F, FontStyle.Bold)
            };

            Label lblEmail = new Label
            {
                Text = "Email:",
                Location = new Point(50, 70),
                AutoSize = true,
                ForeColor = Color.Black
            };
            txtEmail = new TextBox
            {
                Location = new Point(110, 67),
                Width = 200
            };

            Label lblPassword = new Label
            {
                Text = "Password:",
                Location = new Point(35, 110),
                AutoSize = true,
                ForeColor = Color.Black
            };
            txtPassword = new TextBox
            {
                Location = new Point(110, 107),
                Width = 200,
                PasswordChar = '●'
            };

            // Bouton Login
            btnLogin = new Button
            {
                Text = "Login",
                Location = new Point(110, 150),
                Width = 200,
                FlatStyle = FlatStyle.System,
                UseVisualStyleBackColor = true
            };
            // Rendre l'événement asynchrone
            btnLogin.Click += async (sender, e) => await BtnLogin_Click(sender, e);

            // Lien pour s'inscrire (utilise LinkClicked au lieu de Click)
            linkRegister = new LinkLabel
            {
                Text = "Create a new account",
                Location = new Point(140, 185),
                AutoSize = true
            };
            linkRegister.LinkClicked += LinkRegister_LinkClicked;

            this.Controls.Add(lblTitle);
            this.Controls.Add(lblEmail);
            this.Controls.Add(txtEmail);
            this.Controls.Add(lblPassword);
            this.Controls.Add(txtPassword);
            this.Controls.Add(btnLogin);
            this.Controls.Add(linkRegister);
        }

        // Méthode asynchrone pour la gestion du clic sur Login
        private async Task BtnLogin_Click(object sender, EventArgs e)
        {
            string email = txtEmail.Text.Trim();
            string password = txtPassword.Text.Trim();

            if (string.IsNullOrWhiteSpace(email) || string.IsNullOrWhiteSpace(password))
            {
                MessageBox.Show("Please fill in both email and password.",
                                "Error",
                                MessageBoxButtons.OK,
                                MessageBoxIcon.Warning);
                return;
            }

            // Créer une connexion persistante
            TcpClient persistentClient;
            try
            {
                persistentClient = new TcpClient(SERVER_IP, SERVER_PORT);
            }
            catch (Exception ex)
            {
                MessageBox.Show("Unable to connect to server: " + ex.Message);
                return;
            }

            // Envoyer la commande LOGIN avec la connexion persistante de manière asynchrone
            string message = $"LOGIN:{email}:{password}";
            string response = await SendMessageToServerWithClientAsync(persistentClient, message);

            // Après un login réussi
            if (response.Contains("Login successful"))
            {
                // Connexion réussie, passe la connexion persistante à QueriesForm
                QueriesForm queriesForm = new QueriesForm(persistentClient);
                queriesForm.Show();
                this.Hide();
            }
            else
            {
                MessageBox.Show("Invalid credentials or login error.\nServer says: " + response,
                                "Login failed",
                                MessageBoxButtons.OK,
                                MessageBoxIcon.Error);
                persistentClient.Close();
            }
        }

        // Méthode pour envoyer un message avec une connexion existante, en mode asynchrone
        private async Task<string> SendMessageToServerWithClientAsync(TcpClient client, string message)
        {
            try
            {
                NetworkStream stream = client.GetStream();
                byte[] data = Encoding.UTF8.GetBytes(message);

                // Écriture asynchrone
                await stream.WriteAsync(data, 0, data.Length);

                // Lecture asynchrone
                byte[] buffer = new byte[1024];
                int bytesRead = await stream.ReadAsync(buffer, 0, buffer.Length);

                return Encoding.UTF8.GetString(buffer, 0, bytesRead);
            }
            catch (Exception ex)
            {
                return $"Error: {ex.Message}";
            }
        }

        // Événement LinkClicked pour ouvrir RegisterForm
        private void LinkRegister_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
        {
            RegisterForm registerForm = new RegisterForm();
            registerForm.Show();
            this.Hide();
        }
    }
}
