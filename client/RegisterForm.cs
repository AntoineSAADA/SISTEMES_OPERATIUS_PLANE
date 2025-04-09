using System;
using System.Net.Sockets;
using System.IO;
using System.Windows.Forms;
using System.Drawing;

namespace MultiplayerGameClient
{
    public class RegisterForm : Form
    {
        private TextBox txtUsername, txtEmail, txtPassword;
        private Button btnRegister, btnBack;
        private Label lblTitle, lblUsername, lblEmail, lblPassword;

        public RegisterForm()
        {
            InitializeComponent();
        }

        private void InitializeComponent()
        {
            this.Text = "Inscription";
            this.Size = new Size(400, 350);
            this.StartPosition = FormStartPosition.CenterScreen;
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.BackColor = Color.PaleGreen;

            lblTitle = new Label();
            lblTitle.Text = "Créer un nouveau compte";
            lblTitle.Font = new Font("Segoe UI", 14, FontStyle.Bold);
            lblTitle.AutoSize = true;
            lblTitle.Location = new Point(50, 20);

            lblUsername = new Label();
            lblUsername.Text = "Pseudo :";
            lblUsername.AutoSize = true;
            lblUsername.Location = new Point(30, 70);

            txtUsername = new TextBox();
            txtUsername.Location = new Point(150, 65);
            txtUsername.Width = 200;

            lblEmail = new Label();
            lblEmail.Text = "Email :";
            lblEmail.AutoSize = true;
            lblEmail.Location = new Point(30, 110);

            txtEmail = new TextBox();
            txtEmail.Location = new Point(150, 105);
            txtEmail.Width = 200;

            lblPassword = new Label();
            lblPassword.Text = "Mot de passe :";
            lblPassword.AutoSize = true;
            lblPassword.Location = new Point(30, 150);

            txtPassword = new TextBox();
            txtPassword.Location = new Point(150, 145);
            txtPassword.Width = 200;
            txtPassword.PasswordChar = '*';

            btnRegister = new Button();
            btnRegister.Text = "S'inscrire";
            btnRegister.Location = new Point(150, 200);
            btnRegister.Width = 100;
            btnRegister.Click += btnRegister_Click;

            btnBack = new Button();
            btnBack.Text = "Retour";
            btnBack.Location = new Point(260, 200);
            btnBack.Width = 100;
            btnBack.Click += (sender, e) => {
                new LoginForm().Show();
                this.Hide();
            };

            this.Controls.Add(lblTitle);
            this.Controls.Add(lblUsername);
            this.Controls.Add(txtUsername);
            this.Controls.Add(lblEmail);
            this.Controls.Add(txtEmail);
            this.Controls.Add(lblPassword);
            this.Controls.Add(txtPassword);
            this.Controls.Add(btnRegister);
            this.Controls.Add(btnBack);
        }

        private void btnRegister_Click(object sender, EventArgs e)
        {
            // Envoi de la commande d'inscription au serveur (à implémenter selon le protocole)
            // Pour simplifier, on peut afficher un message et revenir au login.
            MessageBox.Show("Inscription non implémentée dans cet exemple.\nRetour à l'écran de connexion.");
            new LoginForm().Show();
            this.Hide();
        }
    }
}
