using System;
using System.Drawing;
using System.Net.Mail;
using System.Net.Sockets;
using System.Text;
using System.Windows.Forms;

namespace ClientApp
{
    public class RegisterForm : Form
    {
        private TextBox txtUsername, txtEmail, txtPassword;
        private Button btnRegister;
        private LinkLabel linkGoLogin;
        
        private const string SERVER_IP = "127.0.0.1";
        private const int SERVER_PORT = 8080;

        public RegisterForm()
        {
            this.Text = "Register";
            this.Size = new Size(400, 300);
            this.StartPosition = FormStartPosition.CenterScreen;
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            //Mettre la couleur de fond a vert pastel
            this.BackColor = Color.PaleGreen;
            
            InitializeComponents();
        }


        private void InitializeComponents()
        {
            Label lblTitle = new Label
            {
                Text = "Create a new account",
                Location = new Point(100, 20),
                AutoSize = true,
                Font = new Font("Segoe UI", 12F, FontStyle.Bold)
            };

            Label lblUsername = new Label
            {
                Text = "Username:",
                Location = new Point(30, 70),
                AutoSize = true,
                ForeColor = Color.Black
            };
            txtUsername = new TextBox
            {
                Location = new Point(110, 67),
                Width = 200
            };

            Label lblEmail = new Label
            {
                Text = "Email:",
                Location = new Point(50, 110),
                AutoSize = true,
                ForeColor = Color.Black
            };
            txtEmail = new TextBox
            {
                Location = new Point(110, 107),
                Width = 200
            };

            Label lblPassword = new Label
            {
                Text = "Password:",
                Location = new Point(30, 150),
                AutoSize = true,
                ForeColor = Color.Black
            };
            txtPassword = new TextBox
            {
                Location = new Point(110, 147),
                Width = 200,
                PasswordChar = '●'
            };

            btnRegister = new Button
            {
                Text = "Register",
                Location = new Point(110, 190),
                Width = 200,
                FlatStyle = FlatStyle.System,
                UseVisualStyleBackColor = true
            };
            btnRegister.Click += BtnRegister_Click;

            linkGoLogin = new LinkLabel
            {
                Text = "Already have an account? Log in",
                Location = new Point(110, 225),
                AutoSize = true
            };
            linkGoLogin.LinkClicked += LinkGoLogin_LinkClicked;

            this.Controls.Add(lblTitle);
            this.Controls.Add(lblUsername);
            this.Controls.Add(txtUsername);
            this.Controls.Add(lblEmail);
            this.Controls.Add(txtEmail);
            this.Controls.Add(lblPassword);
            this.Controls.Add(txtPassword);
            this.Controls.Add(btnRegister);
            this.Controls.Add(linkGoLogin);
        }

        private void LinkGoLogin_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
        {
            LoginForm loginForm = new LoginForm();
            loginForm.Show();
            this.Close();
        }

        private void BtnRegister_Click(object sender, EventArgs e)
        {
            string username = txtUsername.Text.Trim();
            string email = txtEmail.Text.Trim();
            string password = txtPassword.Text.Trim();

            if (string.IsNullOrWhiteSpace(username) ||
                string.IsNullOrWhiteSpace(email) ||
                string.IsNullOrWhiteSpace(password))
            {
                MessageBox.Show("Please fill all fields to register.", 
                                "Error",
                                MessageBoxButtons.OK, 
                                MessageBoxIcon.Warning);
                return;
            }

            // Validation de l'adresse e-mail
            if (!IsValidEmail(email))
            {
                MessageBox.Show("Please enter a valid email address.", 
                                "Invalid Email", 
                                MessageBoxButtons.OK, 
                                MessageBoxIcon.Warning);
                return;
            }

            string message = $"REGISTER:{username}:{email}:{password}";
            string response = SendMessageToServer(message);

            if (response.Contains("successful"))
            {
                MessageBox.Show("Registration successful!\nYou can now log in.", 
                                "Success", 
                                MessageBoxButtons.OK, 
                                MessageBoxIcon.Information);
                
                LoginForm loginForm = new LoginForm();
                loginForm.Show();
                this.Close();
            }
            else
            {
                MessageBox.Show("Registration failed.\nServer says: " + response, 
                                "Error", 
                                MessageBoxButtons.OK, 
                                MessageBoxIcon.Error);
            }
        }

        // Méthode de validation d'un e-mail en utilisant MailAddress
        private bool IsValidEmail(string email)
        {
            try
            {
                MailAddress addr = new MailAddress(email);
                return addr.Address == email;
            }
            catch
            {
                return false;
            }
        }

        private string SendMessageToServer(string message)
        {
            try
            {
                using (TcpClient client = new TcpClient(SERVER_IP, SERVER_PORT))
                using (NetworkStream stream = client.GetStream())
                {
                    byte[] data = Encoding.UTF8.GetBytes(message);
                    stream.Write(data, 0, data.Length);

                    byte[] buffer = new byte[1024];
                    int bytesRead = stream.Read(buffer, 0, buffer.Length);
                    return Encoding.UTF8.GetString(buffer, 0, bytesRead);
                }
            }
            catch (Exception ex)
            {
                return $"Error: {ex.Message}";
            }
        }
    }
}
