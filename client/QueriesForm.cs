using System;
using System.Net.Sockets;
using System.IO;
using System.Threading;
using System.Windows.Forms;
using System.Drawing;

namespace MultiplayerGameClient
{
    public class QueriesForm : Form
    {
        private ListBox lstPlayers;
        private TextBox txtResults;
        private Button btnQuery1, btnQuery2, btnQuery3, btnLogout;

        private TcpClient client;
        private StreamReader reader;
        private StreamWriter writer;
        private Thread listenThread;
        private volatile bool keepListening = true;
        private string username;

        public QueriesForm(TcpClient client, StreamReader reader, StreamWriter writer, string username)
        {
            this.client = client;
            this.reader = reader;
            this.writer = writer;
            this.username = username;
            InitializeComponent();
            StartListening();
        }

        private void InitializeComponent()
        {
            this.Text = "Interface Principale";
            this.Size = new Size(800, 500);
            this.StartPosition = FormStartPosition.CenterScreen;
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            this.BackColor = Color.LightGray;

            Label lblTitle = new Label();
            lblTitle.Text = "Interface de jeu - Bienvenue " + username;
            lblTitle.Font = new Font("Segoe UI", 14, FontStyle.Bold);
            lblTitle.AutoSize = true;
            lblTitle.Location = new Point(20, 20);

            lstPlayers = new ListBox();
            lstPlayers.Location = new Point(20, 60);
            lstPlayers.Size = new Size(200, 380);

            txtResults = new TextBox();
            txtResults.Location = new Point(240, 60);
            txtResults.Size = new Size(520, 300);
            txtResults.Multiline = true;
            txtResults.ScrollBars = ScrollBars.Vertical;

            btnQuery1 = new Button();
            btnQuery1.Text = "QUERY1";
            btnQuery1.Location = new Point(240, 380);
            btnQuery1.Size = new Size(100, 40);
            btnQuery1.Click += btnQuery1_Click;

            btnQuery2 = new Button();
            btnQuery2.Text = "QUERY2";
            btnQuery2.Location = new Point(360, 380);
            btnQuery2.Size = new Size(100, 40);
            btnQuery2.Click += btnQuery2_Click;

            btnQuery3 = new Button();
            btnQuery3.Text = "QUERY3";
            btnQuery3.Location = new Point(480, 380);
            btnQuery3.Size = new Size(100, 40);
            btnQuery3.Click += btnQuery3_Click;

            btnLogout = new Button();
            btnLogout.Text = "LOGOUT";
            btnLogout.Location = new Point(600, 380);
            btnLogout.Size = new Size(100, 40);
            btnLogout.Click += btnLogout_Click;

            this.Controls.Add(lblTitle);
            this.Controls.Add(lstPlayers);
            this.Controls.Add(txtResults);
            this.Controls.Add(btnQuery1);
            this.Controls.Add(btnQuery2);
            this.Controls.Add(btnQuery3);
            this.Controls.Add(btnLogout);
        }

        private void StartListening()
        {
            listenThread = new Thread(ListenLoop);
            listenThread.IsBackground = true;
            listenThread.Start();
        }

        private void ListenLoop()
        {
            try {
                string message;
                while (keepListening && (message = reader.ReadLine()) != null) {
                    if (message.StartsWith("UPDATE_LIST:")) {
                        string list = message.Substring("UPDATE_LIST:".Length);
                        string[] players = list.Split(new char[] {','}, StringSplitOptions.RemoveEmptyEntries);
                        this.Invoke((MethodInvoker) delegate {
                            lstPlayers.Items.Clear();
                            foreach (string p in players) {
                                lstPlayers.Items.Add(p.Trim());
                            }
                        });
                    }
                    else if (message.StartsWith("QUERY1_RESULT:") ||
                             message.StartsWith("QUERY2_RESULT:") ||
                             message.StartsWith("QUERY3_RESULT:"))
                    {
                        this.Invoke((MethodInvoker) delegate {
                            // Optionnel: si on veut remplacer les " | " par des retours à la ligne
                            // string display = message.Replace(" | ", "\r\n");
                            // txtResults.Text = display;
                            
                            // Sinon, on met tout tel quel
                            txtResults.Text = message;
                        });
                    }
                }
            }
            catch (IOException) {
                // La connexion a été fermée.
            }
        }

        private void btnQuery1_Click(object sender, EventArgs e)
        {
            try { writer.WriteLine("QUERY1"); } catch { }
        }
        private void btnQuery2_Click(object sender, EventArgs e)
        {
            try { writer.WriteLine("QUERY2"); } catch { }
        }
        private void btnQuery3_Click(object sender, EventArgs e)
        {
            try { writer.WriteLine("QUERY3"); } catch { }
        }
        private void btnLogout_Click(object sender, EventArgs e)
        {
            try { writer.WriteLine("LOGOUT"); } catch { }
            keepListening = false;
            client.Close();
            Application.Exit();
        }
        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            keepListening = false;
            client.Close();
            base.OnFormClosing(e);
        }
    }
}
