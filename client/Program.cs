using System;
using System.Windows.Forms;

namespace ClientApp
{
    static class Program
    {
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            // On démarre sur la page de connexion
            Application.Run(new LoginForm());
        }
    }
}
