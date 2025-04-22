/* ===========================================================
 *  File :  QueriesForm.cs     (client v5)
 * =========================================================== */
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace MultiplayerGameClient
{
    public class QueriesForm : Form
    {
        /* ----------  UI  ---------- */
        private ListBox   lstPlayers;
        private TextBox   txtResults;
        private Button    btnQuery1, btnQuery2, btnQuery3;
        private Button    btnInvite, btnLogout;

        /* ----------  réseau / synchro  ---------- */
        private readonly TcpClient  client;
        private readonly StreamReader reader;
        private readonly StreamWriter writer;

        private readonly BlockingCollection<string> inbox =
            new BlockingCollection<string>();

        private Thread listenThread;
        private readonly string username;

        public QueriesForm(TcpClient c, StreamReader r,
                           StreamWriter w, string user)
        {
            client   = c; reader = r; writer = w;
            username = user;

            InitializeComponent();
            StartListening();
            StartUiConsumer();
        }

        /* =======================  IHM  ======================= */
        private void InitializeComponent()
        {
            Text           = "Game lobby";
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
                Location      = new Point(20, 60),
                Size          = new Size(200, 380),
                SelectionMode = SelectionMode.MultiExtended
            };

            txtResults = new TextBox {
                Location  = new Point(240, 60),
                Size      = new Size(620, 300),
                Multiline = true,
                ScrollBars= ScrollBars.Vertical
            };

            int yButtons = 380;
            btnQuery1 = MakeBtn("QUERY1", new Point(240, yButtons),
                                (s,e)=> writer.WriteLine("QUERY1"));
            btnQuery2 = MakeBtn("QUERY2", new Point(360, yButtons),
                                (s,e)=> writer.WriteLine("QUERY2"));
            btnQuery3 = MakeBtn("QUERY3", new Point(480, yButtons),
                                (s,e)=> writer.WriteLine("QUERY3"));
            btnInvite = MakeBtn("INVITE", new Point(600, yButtons),
                                btnInvite_Click);
            btnLogout = MakeBtn("LOGOUT", new Point(720, yButtons),
                                btnLogout_Click);

            Controls.AddRange(new Control[]{
                lblTitle, lstPlayers, txtResults,
                btnQuery1, btnQuery2, btnQuery3,
                btnInvite, btnLogout });
        }
        // ── QueriesForm.cs  ──────────────────────────────────────────────
// remplacez entièrement la méthode MakeBtn par la version ci‑dessous
private static Button MakeBtn(string txt, Point loc, EventHandler handler)
{
    var btn = new Button
    {
        Text      = txt,
        Location  = loc,
        Size      = new Size(100, 40)
    };
    if (handler != null)
        btn.Click += handler;
    return btn;
}


        /* ============  réseau : réception asynchrone  ============ */
        private void StartListening()
        {
            listenThread = new Thread(() =>
            {
                try {
                    string line;
                    while ((line = reader.ReadLine()) != null)
                        inbox.Add(line);        // pas d’accès UI ici
                }
                catch { /* socket fermé */ }
                finally { inbox.CompleteAdding(); }
            });
            listenThread.IsBackground = true;
            listenThread.Start();
        }
        /* =================  consommation sur thread UI  ================= */
        private void StartUiConsumer()
        {
            var uiCtx = SynchronizationContext.Current;
            Task.Run(() =>
            {
                foreach (var msg in inbox.GetConsumingEnumerable())
                    uiCtx.Post(_ => Dispatch(msg), null);
            });
        }
        private void Dispatch(string line)
        {
            if (line.StartsWith("UPDATE_LIST:"))
            {
                var names = line.Substring(12)
                                .Split(new[]{','},
                                       StringSplitOptions.RemoveEmptyEntries);
                lstPlayers.Items.Clear();
                foreach (var n in names) lstPlayers.Items.Add(n.Trim());
            }
            else if (line.StartsWith("QUERY"))
                txtResults.Text = line;
            else if (line.StartsWith("INVITE_REQUEST:"))
                HandleInviteRequest(line.Substring(15));
            else if (line.StartsWith("INVITE_RESULT:"))
                HandleInviteResult(line.Substring(14));
            else if (line.StartsWith("CHAT_MSG:")  ||
                     line.StartsWith("MOVE:"))
            {
                // redirection vers GameForm (si ouverte)
                GameForm.Feed(line);
            }
        }

        /* ----------------  invitations  ---------------- */
        private void HandleInviteRequest(string payload)
        {
            var p    = payload.Split(':');
            if (p.Length<2) return;
            var inviter  = p[0];
            var invitees = p[1].Split(',');

            if (Array.IndexOf(invitees, username) == -1) return;

            var dr = MessageBox.Show(
                $"{inviter} invites you to play.\nAccept ?",
                "Invitation", MessageBoxButtons.YesNo,
                MessageBoxIcon.Question);

            writer.WriteLine($"INVITE_RESP:{inviter}:{(dr==DialogResult.Yes?"ACCEPT":"REJECT")}");
        }
        private void HandleInviteResult(string payload)
        {
            var p = payload.Split(':');
            if (p.Length<2) return;
            var inviter = p[0]; var res = p[1];

            if (res=="ACCEPTED")
            {
                MessageBox.Show("Game starting !", "Info");
                var gf = new GameForm(writer, inbox, username);
                gf.Show();
                Hide();
            }
            else
                MessageBox.Show("Invitation cancelled.", "Info");
        }

        /* ----------------  UI : boutons  ---------------- */
        private void btnInvite_Click(object sender, EventArgs e)
        {
            if (lstPlayers.SelectedItems.Count==0)
            {
                MessageBox.Show("Select at least one player."); return;
            }
            var list = new List<string>();
            foreach (var o in lstPlayers.SelectedItems)
                if (o.ToString()!=username) list.Add(o.ToString());
            if (list.Count==0)
            {
                MessageBox.Show("You cannot invite only yourself."); return;
            }
            writer.WriteLine($"INVITE:{string.Join(",",list)}");
            MessageBox.Show("Invitation sent.");
        }
        private void btnLogout_Click(object s, EventArgs e)
        {
            writer.WriteLine("LOGOUT");
            client.Close();
            Application.Exit();
        }
        protected override void OnFormClosing(FormClosingEventArgs e)
        { client.Close(); base.OnFormClosing(e); }
    }
}
