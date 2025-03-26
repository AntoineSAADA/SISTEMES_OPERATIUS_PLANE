using System;
using System.Drawing;
using System.Net.Sockets;
using System.Text;
using System.Windows.Forms;

namespace ClientApp
{
    public class QueriesForm : Form
    {
        private Button btnQuery1, btnQuery2, btnQuery3, btnLogout, btnGetPlayers;
        private TextBox txtResponse;

        // Connexion persistante
        private TcpClient persistentClient;
        private NetworkStream stream;

        // Constructeur recevant le TcpClient persistant
        public QueriesForm(TcpClient client)
        {
            this.persistentClient = client;
            this.stream = client.GetStream();

            this.Text = "Queries";
            this.Size = new Size(600, 400);
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

            btnGetPlayers = new Button
            {
                Text = "GET PLAYERS",
                Location = new Point(380, 60),
                Width = 100,
                Height = 40,
                BackColor = Color.LightYellow,
                ForeColor = Color.Black,
                FlatStyle = FlatStyle.Flat,
                UseVisualStyleBackColor = false
            };
            btnGetPlayers.FlatAppearance.BorderSize = 1;
            btnGetPlayers.Click += BtnGetPlayers_Click;

            btnLogout = new Button
            {
                Text = "Logout",
                Location = new Point(500, 60),
                Width = 80,
                Height = 40,
                BackColor = Color.LightCoral,
                ForeColor = Color.Black,
                FlatStyle = FlatStyle.Flat,
                UseVisualStyleBackColor = false
            };
            btnLogout.FlatAppearance.BorderSize = 1;
            btnLogout.Click += BtnLogout_Click;

            txtResponse = new TextBox
            {
                Multiline = true,
                Location = new Point(20, 120),
                Width = 560,
                Height = 200,
                ScrollBars = ScrollBars.Vertical,
                BackColor = Color.White,
                ForeColor = Color.Black
            };

            this.Controls.Add(lblTitle);
            this.Controls.Add(btnQuery1);
            this.Controls.Add(btnQuery2);
            this.Controls.Add(btnQuery3);
            this.Controls.Add(btnGetPlayers);
            this.Controls.Add(btnLogout);
            this.Controls.Add(txtResponse);
        }

        private string SendMessageToServer(string message)
        {
            try
            {
                byte[] data = Encoding.UTF8.GetBytes(message);
                stream.Write(data, 0, data.Length);

                byte[] buffer = new byte[1024];
                int bytesRead = stream.Read(buffer, 0, buffer.Length);
                return Encoding.UTF8.GetString(buffer, 0, bytesRead);
            }
            catch (Exception ex)
            {
                return $"Error: {ex.Message}";
            }
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

        private void BtnGetPlayers_Click(object sender, EventArgs e)
        {
            string response = SendMessageToServer("GET_PLAYERS");
            txtResponse.Text = response;
        }

        private void BtnLogout_Click(object sender, EventArgs e)
        {
            string response = SendMessageToServer("LOGOUT");
            MessageBox.Show(response, "Logout");
            stream.Close();
            persistentClient.Close();
            LoginForm loginForm = new LoginForm();
            loginForm.Show();
            this.Close();
        }
    }
}
