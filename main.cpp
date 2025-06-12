#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <map>
#include <vector>
#include <fstream>

#include "emscripten.h"
#include "SDL2/SDL.h"
#include "SDL2/SDL_mixer.h"
#include "SDL2/SDL_ttf.h"

struct Coord
{
    Coord(const int x, const int y) : x(x), y(y) {}
    ~Coord() = default;

    int x;
    int y;
};

struct SDL_TextureDeleter
{
    void operator()(SDL_Texture* texture) const
    {
        if (texture != nullptr)
        {
            SDL_DestroyTexture(texture);
        }
    }
};

struct Mix_ChunkDeleter
{
    void operator()(Mix_Chunk* chunk) const
    {
        if (chunk != nullptr)
        {
            Mix_FreeChunk(chunk);
        }
    }
};

struct TTF_FontDeleter
{
    void operator()(TTF_Font* font) const
    {
        if (font != nullptr)
        {
            TTF_CloseFont(font);
        }
    }
};

enum class Texture
{
    HEAD,
    TAIL,
    FRUIT,
    HIGH_SCORE,
    SCORE
};

enum class Sound
{
    CONSUME_FRUIT,
    DEATH
};

enum class Font
{
    SCORE
};

struct Snake
{
    Coord tempDirection = {0, -1};
    Coord direction = {0, -1};
    std::vector<Coord> tiles = {};

    unsigned int lastUpdate = 0;

    const int STARTING_LENGTH = 4;
    const int DEATH_DELAY = 500;

    bool hasDied = false;
    int pauseUntil = 0;
};

struct Globals
{
    const int WINDOW_WIDTH = 640;
    const int WINDOW_HEIGHT = 640;

    const int TILE_COUNT = 40;
    const int TILE_WIDTH = WINDOW_WIDTH / TILE_COUNT;
    const int TILE_HEIGHT = WINDOW_HEIGHT / TILE_COUNT;

    const int GAME_SPEED = 50;

    const int MAX_FRUITS = 10;

    const std::string HIGH_SCORE_PATH = "/save/highscore.txt";

    const int HIGH_SCORE_FONT_SIZE = 24;

    const Coord SCORE_POS = {0, 0};
    const Coord HIGH_SCORE_POS = {0, 20};

    bool fsReady = false;

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;  

    SDL_Event event = {};
    bool quit = false;
    bool pause = false;

    std::map<Texture, std::unique_ptr<SDL_Texture, SDL_TextureDeleter>> textures = {};
    std::map<Sound, std::unique_ptr<Mix_Chunk, Mix_ChunkDeleter>> sounds = {};
    std::map<Font, std::unique_ptr<TTF_Font, TTF_FontDeleter>> fonts = {};

    Snake snake;

    std::vector<Coord> fruit = {};
} glb;

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void filesystem_loaded()
    {
        glb.fsReady = true;
    }
}

void createTexture(Texture id, SDL_Color color)
{
    SDL_Surface* surf = SDL_CreateRGBSurface(0, glb.TILE_WIDTH, glb.TILE_HEIGHT, 32, 0, 0, 0, 0);
    Uint32 uint_color = SDL_MapRGB(surf->format, color.r, color.g, color.b);
    SDL_FillRect(surf, nullptr, uint_color);
    SDL_Texture* text = SDL_CreateTextureFromSurface(glb.renderer, surf);
    SDL_FreeSurface(surf);
    glb.textures[id] = std::unique_ptr<SDL_Texture, SDL_TextureDeleter>(text);
}

void createTexture(Texture id, Font font, SDL_Color color, std::string text)
{
    SDL_Surface* surf = TTF_RenderText_Blended(glb.fonts[font].get(), text.c_str(), color);
    SDL_Texture* texture = SDL_CreateTextureFromSurface(glb.renderer, surf);
    SDL_FreeSurface(surf);
    glb.textures[id] = std::unique_ptr<SDL_Texture, SDL_TextureDeleter>(texture);
}

void render(const Coord& pos, SDL_Texture* texture)
{
    if (texture != nullptr)
    {
        SDL_Rect dest = {pos.x * glb.TILE_WIDTH, pos.y * glb.TILE_HEIGHT, glb.TILE_WIDTH, glb.TILE_HEIGHT};
        SDL_RenderCopy(glb.renderer, texture, nullptr, &dest);
    }
}

void renderOffTile(const Coord& pos, SDL_Texture* texture)
{
    if (texture != nullptr)
    {
        int width, height;
        SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);
        SDL_Rect dest = {pos.x, pos.y, width, height};
        SDL_RenderCopy(glb.renderer, texture, nullptr, &dest);
    }
}

void loadSound(Sound id, std::string filepath)
{
    Mix_Chunk* chunk = Mix_LoadWAV(filepath.c_str());
    glb.sounds[id] = std::unique_ptr<Mix_Chunk, Mix_ChunkDeleter>(chunk);
}

void loadFont(Font id, std::string filepath, int size)
{
    TTF_Font* font = TTF_OpenFont(filepath.c_str(), size);
    glb.fonts[id] = std::unique_ptr<TTF_Font, TTF_FontDeleter>(font);
}

void initFileSystem()
{
    EM_ASM(
        FS.mkdir("/save");
        FS.mount(IDBFS, {}, "/save");
        FS.syncfs(true, function (err) {
            if (err) {
                console.log("Error syncing filesystem: ", err);
            } else {
                ccall("filesystem_loaded", "void", [], []);
            }
        });
    );
}

void playSound(Sound id)
{
    Mix_PlayChannel(-1, glb.sounds[id].get(), 0);
}

int loadHighScore()
{
    std::string filepath = glb.HIGH_SCORE_PATH;
    std::ifstream ifs(filepath);
    if (ifs.is_open())
    {
        int score;
        ifs >> score;
        ifs.close();
        return score;
    }
    return 0;
}

void saveHighScore(int score)
{
    std::string filepath = glb.HIGH_SCORE_PATH;
    std::ofstream ofs(filepath);
    if (ofs.is_open())
    {
        ofs << score;
        ofs.close();

        EM_ASM(
            FS.syncfs(false, function (err) {
                if (err) {
                    console.log("Error syncing to IndexedDB: ", err);
                }
            });
        );
    }
}

void updateHighScore()
{
    auto it = glb.textures.find(Texture::HIGH_SCORE);
    if (it != glb.textures.end()) 
    {
        it->second.reset();
    }
    std::string highScoreText = "HIGHSCORE: " + std::to_string(loadHighScore());
    createTexture(Texture::HIGH_SCORE, Font::SCORE, {255, 255, 255}, highScoreText);
}

void updateScore()
{
    auto it = glb.textures.find(Texture::SCORE);
    if (it != glb.textures.end()) 
    {
        it->second.reset();
    }
    std::string scoreText = "SCORE: " + std::to_string(glb.snake.tiles.size());
    createTexture(Texture::SCORE, Font::SCORE, {255, 255, 255}, scoreText);
}

void consumeFruit(int j)
{
    int max = glb.snake.tiles.size() - 1;
    Coord diff = {glb.snake.tiles[max].x - glb.snake.tiles[max - 1].x, glb.snake.tiles[max].y - glb.snake.tiles[max - 1].y};
    glb.snake.tiles.emplace_back(glb.snake.tiles[max].x + diff.x, glb.snake.tiles[max].y + diff.y);

    glb.fruit.erase(glb.fruit.begin() + j);

    playSound(Sound::CONSUME_FRUIT);
}

void addFruit()
{
    std::vector<Coord> possible = {};
    for (int i = 0; i < glb.TILE_COUNT; i++)
    {
        for (int j = 0; j < glb.TILE_COUNT; j++)
        {
            possible.push_back({i, j});
        }
    }

    for (int k = 0; k < glb.fruit.size(); k++)
    {
        possible.erase(
            std::remove_if(
                possible.begin(), 
                possible.end(),
                [&](const Coord& c) { return c.x == glb.fruit[k].x && c.y == glb.fruit[k].y; }
            ),
            possible.end()
        );
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, possible.size() - 1);
    int randomIndex = distrib(gen);

    glb.fruit.push_back(possible[randomIndex]);

    updateScore();
}

void resetSnake()
{
    if (glb.snake.hasDied)
    {
        playSound(Sound::DEATH);
        glb.snake.pauseUntil = SDL_GetTicks() + glb.snake.DEATH_DELAY;
        int score = glb.snake.tiles.size() - 1;
        int highScore = loadHighScore();
        if (score > highScore)
        {
            saveHighScore(score);
        }
        updateHighScore();
    }

    for (int j = 0; j < glb.fruit.size(); j++)
    {
        consumeFruit(j);
        addFruit();
    }

    glb.snake.tempDirection = {0, -1};
    glb.snake.direction = {0, -1};
    glb.snake.tiles.clear();

    for (int i = 0; i < glb.snake.STARTING_LENGTH; i++)
    {
        glb.snake.tiles.emplace_back((glb.TILE_COUNT / 2), (glb.TILE_COUNT / 2) + i);
    }

    updateScore();
}

void updateSnake()
{
    for (int i = 0; i < glb.snake.tiles.size(); i++)
    {
        int currentIndex = (glb.snake.tiles.size() - i) - 1;
        if (currentIndex != 0)
        {
            glb.snake.tiles[currentIndex].x = glb.snake.tiles[currentIndex - 1].x;
            glb.snake.tiles[currentIndex].y = glb.snake.tiles[currentIndex - 1].y;
        } else
        {
            glb.snake.tiles[currentIndex].x += glb.snake.direction.x;
            glb.snake.tiles[currentIndex].y += glb.snake.direction.y;
        }

        if (glb.snake.tiles[i].y >= glb.TILE_COUNT)
        {
            glb.snake.tiles[i].y = 0;
        }
        if (glb.snake.tiles[i].y < 0)
        {
            glb.snake.tiles[i].y = glb.TILE_COUNT;
        }
        if (glb.snake.tiles[i].x >= glb.TILE_COUNT)
        {
            glb.snake.tiles[i].x = 0;
        }
        if (glb.snake.tiles[i].x < 0)
        {
            glb.snake.tiles[i].x = glb.TILE_COUNT;
        }

        if (i != 0 && glb.snake.tiles[0].x == glb.snake.tiles[i].x && glb.snake.tiles[0].y == glb.snake.tiles[i].y)
        {
            glb.snake.hasDied = true;
            resetSnake();
        }
    }

    for (int j = 0; j < glb.fruit.size(); j++)
    {
        if (glb.snake.tiles[0].x == glb.fruit[j].x && glb.snake.tiles[0].y == glb.fruit[j].y)
        {
            consumeFruit(j);
            addFruit();
        }
    }

    glb.snake.lastUpdate = SDL_GetTicks();
}

void init()
{
    SDL_Init(SDL_INIT_EVERYTHING);

    Mix_Init(0);
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024);

    TTF_Init();

    glb.window = SDL_CreateWindow(
        "Snake Web", 
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
        glb.WINDOW_WIDTH, glb.WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN
    );

    glb.renderer = SDL_CreateRenderer(glb.window, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetRenderDrawColor(glb.renderer, 0x00, 0x00, 0x00, 0x00);

    createTexture(Texture::HEAD, {255, 0, 0});
    createTexture(Texture::TAIL, {0, 0, 255});
    createTexture(Texture::FRUIT, {0, 255, 0});

    loadSound(Sound::CONSUME_FRUIT, "assets/consume.wav");
    loadSound(Sound::DEATH, "assets/death.wav");

    loadFont(Font::SCORE, "assets/Pixellari.ttf", glb.HIGH_SCORE_FONT_SIZE);

    resetSnake();

    for (int i = 0; i < glb.MAX_FRUITS; i++)
    {
        addFruit();
    }
}

void cleanup()
{
    if (glb.renderer != nullptr)
    {
        SDL_DestroyRenderer(glb.renderer);
    }

    if (glb.window != nullptr)
    {
        SDL_DestroyWindow(glb.window);
    }

    TTF_Quit();
    Mix_Quit();
    SDL_Quit();
}

void loop()
{
    while (SDL_PollEvent(&glb.event))
    {
        switch (glb.event.type)
        {
            case SDL_QUIT:
            {
                glb.quit = true;
                break;
            }
            case SDL_KEYDOWN:
            {
                SDL_Keycode sym = glb.event.key.keysym.sym;
                if (sym == SDLK_UP && glb.snake.direction.y != 1)
                {
                    glb.snake.tempDirection.x = 0;
                    glb.snake.tempDirection.y = -1;
                }
                else if (sym == SDLK_DOWN && glb.snake.direction.y != -1)
                {
                    glb.snake.tempDirection.x = 0;
                    glb.snake.tempDirection.y = 1;
                }
                else if (sym == SDLK_LEFT && glb.snake.direction.x != 1)
                {
                    glb.snake.tempDirection.x = -1;
                    glb.snake.tempDirection.y = 0;
                }
                else if (sym == SDLK_RIGHT && glb.snake.direction.x != -1)
                {
                    glb.snake.tempDirection.x = 1;
                    glb.snake.tempDirection.y = 0;
                }
                else if (sym == SDLK_ESCAPE)
                {
                    if (glb.pause)
                    {
                        glb.pause = false;
                    } else
                    {
                        glb.pause = true;
                    }
                }
                break;
            }
            default:
            {
                break;
            }
        }
    }

    if (SDL_GetTicks() > glb.snake.pauseUntil)
    {
        if (SDL_GetTicks() - glb.snake.lastUpdate > glb.GAME_SPEED && !glb.pause)
        {
            glb.snake.direction = glb.snake.tempDirection;
            updateSnake();
        }

        SDL_SetRenderDrawColor(glb.renderer, 0, 0, 0, 255);
        
        SDL_RenderClear(glb.renderer);

        for (int i = 0; i < glb.fruit.size(); i++)
        {
            render(glb.fruit[i], glb.textures[Texture::FRUIT].get());
        }

        for (int i = 0; i < glb.snake.tiles.size(); i++)
        {
            Texture texture;
            if (i == 0)
            {
                texture = Texture::HEAD;
            } else
            {
                texture = Texture::TAIL;
            }

            render(glb.snake.tiles[i], glb.textures[texture].get());
        }

        renderOffTile(glb.SCORE_POS, glb.textures[Texture::SCORE].get());
        renderOffTile(glb.HIGH_SCORE_POS, glb.textures[Texture::HIGH_SCORE].get());

        
        SDL_RenderPresent(glb.renderer);
    }

    if (glb.quit)
    {
        emscripten_cancel_main_loop();
        cleanup();
    }
}

void waitForFS()
{
    if (!glb.fsReady)
    {
        emscripten_async_call(reinterpret_cast<void (*)(void *)>(waitForFS), nullptr, 100);
        return;
    }

    updateScore();
    updateHighScore();

    emscripten_set_main_loop(loop, 0, 1);
    emscripten_set_main_loop_timing(EM_TIMING_RAF, 0);
}

int main()
{
    init();

    initFileSystem();
    waitForFS();

    return 0;
}