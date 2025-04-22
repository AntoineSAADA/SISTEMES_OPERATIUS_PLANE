//  QueriesForm.cs – v4 (invitation support, layout corrigé, compatible C# 7.x)
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Net.Sockets;
using System.Threading;
using System.Windows.Forms;

namespace MultiplayerGameClient
{
    public class QueriesForm : Form
    {
        private ListBox lstPlayers;
        private TextBox txtResults;
        private Button  btnQuery1, btnQuery2, btnQuery3, btnInvite, btnLogout;

        private readonly TcpClient    client;
        private readonly StreamReader reader;
        private readonly StreamWriter writer;
        private Thread   listenThread;
        private volatile bool keepListening = true;
        private readonly string username;

        public QueriesForm(TcpClient c, StreamReader r, StreamWriter w, string user)
        {
            client = c; reader = r; writer = w; username = user;
            InitializeComponent();
            StartListening();
        }

        private void InitializeComponent()
        {
            Text           = "Game Interface";
            Size           = new Size(900, 520);
            FormBorderStyle= FormBorderStyle.FixedDialog;
            MaximizeBox    = false;
            BackColor      = Color.LightGray;
            StartPosition  = FormStartPosition.CenterScreen;

            var lblTitle = new Label {
                Text = $"Welcome {username}",
                Font = new Font("Segoe UI", 14, FontStyle.Bold),
                AutoSize = true,
                Location = new Point(20, 20)
            };

            lstPlayers = new ListBox {
                Location = new Point(20, 60),
                Size     = new Size(200, 380),
                SelectionMode = SelectionMode.MultiExtended
            };

            txtResults = new TextBox {
                Location = new Point(240, 60),
                Size     = new Size(620, 300),
                Multiline = true,
                ScrollBars = ScrollBars.Vertical
            };

            int yButtons = 380;

            btnQuery1 = new Button {
                Text = "QUERY1", Location = new Point(240, yButtons), Size = new Size(100, 40)
            }; btnQuery1.Click += (s, e) => SendCommand("QUERY1");

            btnQuery2 = new Button {
                Text = "QUERY2", Location = new Point(360, yButtons), Size = new Size(100, 40)
            }; btnQuery2.Click += (s, e) => SendCommand("QUERY2");

            btnQuery3 = new Button {
                Text = "QUERY3", Location = new Point(480, yButtons), Size = new Size(100, 40)
            }; btnQuery3.Click += (s, e) => SendCommand("QUERY3");

            btnInvite = new Button {
                Text = "INVITE", Location = new Point(600, yButtons), Size = new Size(100, 40)
            }; btnInvite.Click += btnInvite_Click;

            btnLogout = new Button {
                Text = "LOGOUT", Location = new Point(720, yButtons), Size = new Size(100, 40)
            }; btnLogout.Click += btnLogout_Click;

            Controls.AddRange(new Control[] {
                lblTitle, lstPlayers, txtResults,
                btnQuery1, btnQuery2, btnQuery3, btnInvite, btnLogout
            });
        }

        private void SendCommand(string cmd) { try { writer.WriteLine(cmd); } catch { } }

        /* ----------  network listener ---------- */
        private void StartListening()
        {
            listenThread = new Thread(ListenLoop) { IsBackground = true };
            listenThread.Start();
        }
        private void ListenLoop()
        {
            try {
                string line;
                while (keepListening && (line = reader.ReadLine()) != null) {
                    if (line.StartsWith("UPDATE_LIST:")) {
                        string[] players = line.Substring(12)
                                               .Split(new[] { ',' },
                                                      StringSplitOptions.RemoveEmptyEntries);
                        BeginInvoke((MethodInvoker)delegate {
                            lstPlayers.Items.Clear();
                            foreach (string p in players) lstPlayers.Items.Add(p.Trim());
                        });
                    }
                    else if (line.StartsWith("QUERY")) {
                        BeginInvoke((MethodInvoker)(() => txtResults.Text = line));
                    }
                    else if (line.StartsWith("INVITE_REQUEST:"))
                        HandleInviteRequest(line.Substring(15));
                    else if (line.StartsWith("INVITE_RESULT:"))
                        HandleInviteResult(line.Substring(14));
                }
            }
            catch (IOException) { /* connexion coupée */ }
        }

        /* ----------  invitation handlers ---------- */
        private void HandleInviteRequest(string payload)
        {
            // format "inviter:invitee1,invitee2,…"
            string[] parts = payload.Split(':');
            if (parts.Length < 2) return;
            string inviter = parts[0];
            string[] invites = parts[1].Split(',');

            if (!Array.Exists(invites, p => p == username)) return;

            BeginInvoke((MethodInvoker)delegate {
                DialogResult dlg = MessageBox.Show(
                    $"{inviter} invites you to play.\nAccept?",
                    "Game invitation",
                    MessageBoxButtons.YesNo, MessageBoxIcon.Question);

                string reply = dlg == DialogResult.Yes ? "ACCEPT" : "REJECT";
                try { writer.WriteLine($"INVITE_RESP:{inviter}:{reply}"); } catch { }
            });
        }
        private void HandleInviteResult(string payload)
        {
            // format "inviter:ACCEPTED/REJECTED"
            string[] parts = payload.Split(':');
            if (parts.Length < 2) return;
            string inviter = parts[0], result = parts[1];

            BeginInvoke((MethodInvoker)delegate {
                MessageBox.Show(
                    result == "ACCEPTED"
                        ? $"Game with {inviter} will start – everyone accepted!"
                        : $"Game with {inviter} was cancelled – someone declined.",
                    "Invitation result",
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
            });
        }

        /* ----------  UI events ---------- */
        private void btnInvite_Click(object sender, EventArgs e)
        {
            if (lstPlayers.SelectedItems.Count == 0) {
                MessageBox.Show("Select at least one player."); return;
            }
            var targets = new List<string>();
            foreach (var item in lstPlayers.SelectedItems) {
                string p = item.ToString();
                if (p != username) targets.Add(p);
            }
            if (targets.Count == 0) {
                MessageBox.Show("You can’t invite only yourself."); return;
            }
            string csv = string.Join(",", targets);
            try { writer.WriteLine($"INVITE:{csv}"); } catch { }
            MessageBox.Show("Invitation sent.");
        }

        private void btnLogout_Click(object sender, EventArgs e)
        {
            keepListening = false;
            SendCommand("LOGOUT");
            client.Close();
            Application.Exit();
        }
        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            keepListening = false; client.Close();
            base.OnFormClosing(e);
        }
    }
}
