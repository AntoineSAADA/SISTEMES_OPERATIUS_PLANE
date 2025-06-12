using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Content;


namespace ClientGame
{
    public class DogfightGame : Game
    {
        private GraphicsDeviceManager graphics;
        private SpriteBatch spriteBatch;

        private IScreen currentScreen;

        public DogfightGame()
        {
            graphics = new GraphicsDeviceManager(this);
            Content.RootDirectory = "Content";
            IsMouseVisible = true;

            currentScreen = new LoginScreen(this); // ← ICI !
        }

        protected override void Initialize()
        {
            base.Initialize();
        }

        protected override void LoadContent()
        {
            spriteBatch = new SpriteBatch(GraphicsDevice);
            currentScreen.LoadContent(Content); // ici, il est déjà initialisé
        }

        protected override void Update(GameTime gameTime)
        {
            currentScreen.Update(gameTime);
            base.Update(gameTime);
        }

        protected override void Draw(GameTime gameTime)
        {
            GraphicsDevice.Clear(Color.CornflowerBlue);
            spriteBatch.Begin();
            currentScreen.Draw(spriteBatch);
            spriteBatch.End();
            base.Draw(gameTime);
        }

        public void ChangeScreen(IScreen next)
        {
            currentScreen = next;
            currentScreen.LoadContent(Content);
        }
    }
}