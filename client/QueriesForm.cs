using System;
using System.Drawing;
using System.Net.Sockets;
using System.Text;
using System.Windows.Forms;

namespace ClientApp
{
    public class QueriesForm : Form
    {
        private Button btnQuery1, btnQuery2, btnQuery3, btnDisconnect;
        private TextBox txtResponse;
        private string serverIP;
        private int serverPort;

        public QueriesForm(string ip, int port)
        {
            this.serverIP = ip;
            this.serverPort = port;

            this.Text = "Queries";
            this.Size = new Size(500, 350);
            this.StartPosition = FormStartPosition.CenterScreen;
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;

            InitializeComponents();
        }

        private void InitializeComponents()
        {
            Label lblTitle = new Label
            {
                Text = "Welcome! You can run queries below:",
                Location = new Point(20, 20),
                AutoSize = true,
                Font = new Font("Segoe UI", 10F, FontStyle.Bold),
                ForeColor = Color.Black
            };

            btnQuery1 = new Button
            {
                Text = "QUERY1",
                Location = new Point(20, 60),
                Width = 100,
                Height = 40,
                BackColor = Color.WhiteSmoke,
                ForeColor = Color.Black,
                FlatStyle = FlatStyle.Flat,
                UseVisualStyleBackColor = false
            };
            btnQuery1.FlatAppearance.BorderSize = 1;
            btnQuery1.Click += BtnQuery1_Click;

            btnQuery2 = new Button
            {
                Text = "QUERY2",
                Location = new Point(140, 60),
                Width = 100,
                Height = 40,
                BackColor = Color.WhiteSmoke,
                ForeColor = Color.Black,
                FlatStyle = FlatStyle.Flat,
                UseVisualStyleBackColor = false
            };
            btnQuery2.FlatAppearance.BorderSize = 1;
            btnQuery2.Click += BtnQuery2_Click;

            btnQuery3 = new Button
            {
                Text = "QUERY3",
                Location = new Point(260, 60),
                Width = 100,
                Height = 40,
                BackColor = Color.WhiteSmoke,
                ForeColor = Color.Black,
                FlatStyle = FlatStyle.Flat,
                UseVisualStyleBackColor = false
            };
            btnQuery3.FlatAppearance.BorderSize = 1;
            btnQuery3.Click += BtnQuery3_Click;

            // Bouton de déconnexion
            btnDisconnect = new Button
            {
                Text = "Logout",
                Location = new Point(380, 60),
                Width = 80,
                Height = 40,
                BackColor = Color.LightCoral,
                ForeColor = Color.Black,
                FlatStyle = FlatStyle.Flat,
                UseVisualStyleBackColor = false
            };
            btnDisconnect.FlatAppearance.BorderSize = 1;
            btnDisconnect.Click += BtnDisconnect_Click;

            txtResponse = new TextBox
            {
                Multiline = true,
                Location = new Point(20, 120),
                Width = 440,
                Height = 150,
                ScrollBars = ScrollBars.Vertical,
                BackColor = Color.White,
                ForeColor = Color.Black
            };

            this.Controls.Add(lblTitle);
            this.Controls.Add(btnQuery1);
            this.Controls.Add(btnQuery2);
            this.Controls.Add(btnQuery3);
            this.Controls.Add(btnDisconnect);
            this.Controls.Add(txtResponse);
        }

        private void BtnQuery1_Click(object sender, EventArgs e)
        {
            string response = SendMessageToServer("QUERY1");
            txtResponse.Text = response;
        }

        private void BtnQuery2_Click(object sender, EventArgs e)
        {
            string response = SendMessageToServer("QUERY2");
            txtResponse.Text = response;
        }

        private void BtnQuery3_Click(object sender, EventArgs e)
        {
            string response = SendMessageToServer("QUERY3");
            txtResponse.Text = response;
        }

        // Bouton de déconnexion : retourne à la fenêtre de Login
        private void BtnDisconnect_Click(object sender, EventArgs e)
        {
            LoginForm loginForm = new LoginForm();
            loginForm.Show();
            this.Close();
        }

        private string SendMessageToServer(string message)
        {
            try
            {
                using (TcpClient client = new TcpClient(serverIP, serverPort))
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
