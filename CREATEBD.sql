DROP TABLE IF EXISTS history;
DROP TABLE IF EXISTS game;
DROP TABLE IF EXISTS players;


-- ========================================
-- 1) Create the Players table
--    - Stores basic player info.
--    - Includes a unique email constraint.
--    - Passwords should ideally be stored in hashed form.
-- ========================================
CREATE TABLE players (
    id_player    INT AUTO_INCREMENT PRIMARY KEY,
    username     VARCHAR(50) NOT NULL,
    email        VARCHAR(100) NOT NULL UNIQUE,
    password     VARCHAR(255) NOT NULL,   -- Use hashed passwords in production
    total_score  INT DEFAULT 0,
    last_login   DATETIME
);

-- ========================================
-- 2) Create the Game table
--    - Holds game info (name, creation date, status).
--    - winner_id references a player who won the game.
-- ========================================
CREATE TABLE game (
    id_game     INT AUTO_INCREMENT PRIMARY KEY,
    name        VARCHAR(50) NOT NULL,
    created_at  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    status      VARCHAR(20) NOT NULL,
    winner_id   INT,
    -- Foreign key reference to the players table
    CONSTRAINT fk_winner
        FOREIGN KEY (winner_id) 
        REFERENCES players(id_player)
        ON DELETE SET NULL         -- If a player is deleted, set winner_id to NULL
        ON UPDATE CASCADE
);

-- ========================================
-- 3) Create the History table
--    - Links a player to a specific game.
--    - Stores in-game performance stats (kills, deaths, score, etc.).
-- ========================================
CREATE TABLE history (
    id_history  INT AUTO_INCREMENT PRIMARY KEY,
    id_player   INT NOT NULL,
    id_game     INT NOT NULL,
    kills       INT DEFAULT 0,
    deaths      INT DEFAULT 0,
    score       INT DEFAULT 0,
    duration    INT,  -- Duration in seconds or any suitable unit
    timestamp   DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    -- Foreign key references for players and game
    CONSTRAINT fk_player
        FOREIGN KEY (id_player) 
        REFERENCES players(id_player)
        ON DELETE CASCADE          -- If a player is deleted, remove related history
        ON UPDATE CASCADE,
    CONSTRAINT fk_game
        FOREIGN KEY (id_game) 
        REFERENCES game(id_game)
        ON DELETE CASCADE          -- If a game is deleted, remove related history
        ON UPDATE CASCADE
);


INSERT INTO players (username, email, password, total_score, last_login)
VALUES
  ('Alice',    'alice@example.com',   'password1',  120, '2025-03-01 08:00:00'),
  ('Bob',      'bob@example.com',     'password2',   80, '2025-03-02 09:15:00'),
  ('Charlie',  'charlie@example.com', '12345',      150, '2025-03-03 11:45:00'),
  ('Diana',    'diana@example.com',   'password',    60, '2025-03-04 10:00:00'),
  ('Ethan',    'ethan@example.com',   'pass123',    200, '2025-03-05 14:30:00'),
  ('Fiona',    'fiona@example.com',   'qwerty',     110, '2025-03-06 17:20:00'),
  ('George',   'george@example.com',  'letmein',     90, '2025-03-07 12:50:00'),
  ('Hannah',   'hannah@example.com',  'pwd123',     130, '2025-03-08 09:00:00'),
  ('Ian',      'ian@example.com',     'password3',   70, '2025-03-09 18:10:00'),
  ('Jade',     'jade@example.com',    'test123',    140, '2025-03-10 20:00:00');

INSERT INTO game (name, created_at, status, winner_id)
VALUES
  ('First Blood',        '2025-03-01 09:00:00', 'finished',   3),
  ('Pro Battle',         '2025-03-02 10:30:00', 'finished',   1),
  ('Fun Arena',          '2025-03-03 15:45:00', 'in_progress', NULL),
  ('Champions League',   '2025-03-04 20:00:00', 'finished',   5),
  ('Last Stand',         '2025-03-05 17:15:00', 'finished',   8);

INSERT INTO history (id_player, id_game, kills, deaths, score, duration, timestamp)
VALUES
  -- Partie 1: First Blood (id_game = 1)
  (1, 1, 2, 4,  50,  360, '2025-03-01 09:15:00'),
  (2, 1, 3, 3,  60,  360, '2025-03-01 09:15:00'),
  (3, 1, 5, 2, 100,  360, '2025-03-01 09:15:00'),

  -- Partie 2: Pro Battle (id_game = 2)
  (1, 2, 6, 3, 120,  400, '2025-03-02 10:50:00'),
  (2, 2, 2, 5,  40,  400, '2025-03-02 10:50:00'),
  (3, 2, 4, 4,  80,  400, '2025-03-02 10:50:00'),

  -- Partie 3: Fun Arena (id_game = 3)
  (4, 3,  2, 1,  30,  200, '2025-03-03 16:00:00'),
  (5, 3,  4, 3,  65,  200, '2025-03-03 16:00:00'),
  (6, 3,  3, 4,  55,  200, '2025-03-03 16:00:00'),

  -- Partie 4: Champions League (id_game = 4)
  (5, 4,  7, 2, 150,  500, '2025-03-04 20:30:00'),
  (7, 4,  3, 4,  80,  500, '2025-03-04 20:30:00'),
  (8, 4,  2, 5,  60,  500, '2025-03-04 20:30:00'),

  -- Partie 5: Last Stand (id_game = 5)
  (8,  5, 5, 2, 120, 600, '2025-03-05 17:45:00'),
  (9,  5, 3, 4,  70, 600, '2025-03-05 17:45:00'),
  (10, 5, 2, 5,  40, 600, '2025-03-05 17:45:00');


