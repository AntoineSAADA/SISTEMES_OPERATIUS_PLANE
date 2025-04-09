using System;
using System.Net.Sockets;
using System.IO;
using System.Windows.Forms;
using System.Drawing;
using System.Threading.Tasks;

namespace MultiplayerGameClient
{
    public class LoginForm : Form
    {
        private TextBox txtServer;
        private TextBox txtUsername;
        private TextBox txtPassword;
        private Button btnLogin;
        private Label lblTitle, lblServer, lblUsername, lblPassword;

        public LoginForm()
        {
            InitializeComponent();
        }

        private void InitializeComponent()
        {
            this.Text = "Login";
            this.Size = new Size(400, 300);
            this.StartPosition = FormStartPosition.CenterScreen;
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            this.BackColor = Color.SkyBlue;

            lblTitle = new Label();
            lblTitle.Text = "Veuillez vous connecter";
            lblTitle.Font = new Font("Segoe UI", 14, FontStyle.Bold);
            lblTitle.AutoSize = true;
            lblTitle.Location = new Point(80, 20);

            lblServer = new Label();
            lblServer.Text = "Serveur (IP):";
            lblServer.AutoSize = true;
            lblServer.Location = new Point(30, 70);

            txtServer = new TextBox();
            txtServer.Text = "127.0.0.1";
            txtServer.Location = new Point(150, 65);
            txtServer.Width = 200;

            lblUsername = new Label();
            lblUsername.Text = "Pseudo:";
            lblUsername.AutoSize = true;
            lblUsername.Location = new Point(30, 110);

            txtUsername = new TextBox();
            txtUsername.Location = new Point(150, 105);
            txtUsername.Width = 200;

            lblPassword = new Label();
            lblPassword.Text = "Mot de passe:";
            lblPassword.AutoSize = true;
            lblPassword.Location = new Point(30, 150);

            txtPassword = new TextBox();
            txtPassword.Location = new Point(150, 145);
            txtPassword.Width = 200;
            txtPassword.PasswordChar = '*';

            btnLogin = new Button();
            btnLogin.Text = "Login";
            btnLogin.Location = new Point(150, 190);
            btnLogin.Width = 100;
            btnLogin.Click += async (sender, e) => await BtnLogin_Click(sender, e);

            this.Controls.Add(lblTitle);
            this.Controls.Add(lblServer);
            this.Controls.Add(txtServer);
            this.Controls.Add(lblUsername);
            this.Controls.Add(txtUsername);
            this.Controls.Add(lblPassword);
            this.Controls.Add(txtPassword);
            this.Controls.Add(btnLogin);
        }

        private async Task BtnLogin_Click(object sender, EventArgs e)
        {
            string serverIp = txtServer.Text.Trim();
            string username = txtUsername.Text.Trim();
            string password = txtPassword.Text.Trim();

            if (username == "" || password == "")
            {
                MessageBox.Show("Veuillez renseigner le pseudo et le mot de passe.");
                return;
            }

            try
            {
                TcpClient client = new TcpClient();
                await client.ConnectAsync(serverIp, 12345);
                NetworkStream netStream = client.GetStream();
                StreamReader reader = new StreamReader(netStream);
                StreamWriter writer = new StreamWriter(netStream) { AutoFlush = true };

                writer.WriteLine($"LOGIN:{username}:{password}");
                string response = await reader.ReadLineAsync();
                if (response == "Login successful")
                {
                    QueriesForm qf = new QueriesForm(client, reader, writer, username);
                    qf.Show();
                    this.Hide();
                }
                else
                {
                    MessageBox.Show("Échec de l'authentification. Réponse du serveur : " + response);
                    client.Close();
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show("Erreur de connexion : " + ex.Message);
            }
        }
    }
}
