using System;

namespace ClientGame
{
    public static class Program
    {
        [STAThread]
        static void Main()
        {
            using (var game = new DogfightGame())
                game.Run();
        }
    }
}
