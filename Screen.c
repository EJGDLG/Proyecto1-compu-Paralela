
#include <SDL2/SDL.h>
#include <pthread.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>

#define NUM_SHAPES 5
#define POINTS_PER_SHAPE 6
#define SIM_FPS 60

typedef struct {
    float x, y, vx, vy;
} Point;

typedef struct {
    Point points[POINTS_PER_SHAPE];
    SDL_Color color;
    pthread_mutex_t lock;
} Shape;

Shape shapes[NUM_SHAPES];
int winW = 800, winH = 600;
bool running = true;

Uint32 palette[][3] = {
    {0,170,255}, {255,0,170}, {255,255,0},
    {0,255,0}, {255,128,0}, {128,0,255}, {255,0,0}
};
int palette_size = sizeof(palette)/sizeof(palette[0]);

float frand(float min, float max) {
    return min + (float)rand() / RAND_MAX * (max - min);
}

void *simulate(void *arg) {
    Shape *s = (Shape*)arg;
    Uint32 frameDelay = 1000 / SIM_FPS;

    while (running) {
        Uint32 start = SDL_GetTicks();
        pthread_mutex_lock(&s->lock);
        for (int i=0; i<POINTS_PER_SHAPE; i++) {
            s->points[i].x += s->points[i].vx;
            s->points[i].y += s->points[i].vy;

            if (s->points[i].x < 0 || s->points[i].x > winW) s->points[i].vx *= -1;
            if (s->points[i].y < 0 || s->points[i].y > winH) s->points[i].vy *= -1;
        }
        pthread_mutex_unlock(&s->lock);

        Uint32 elapsed = SDL_GetTicks() - start;
        if (elapsed < frameDelay) SDL_Delay(frameDelay - elapsed);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    srand(time(NULL));
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *window = SDL_CreateWindow("Mystify C", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          winW, winH, SDL_WINDOW_SHOWN);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    // Crear shapes
    pthread_t threads[NUM_SHAPES];
    for (int s=0; s<NUM_SHAPES; s++) {
        for (int i=0; i<POINTS_PER_SHAPE; i++) {
            shapes[s].points[i].x = frand(50, winW-50);
            shapes[s].points[i].y = frand(50, winH-50);
            float angle = frand(0, 2*M_PI);
            shapes[s].points[i].vx = cosf(angle) * frand(2,5);
            shapes[s].points[i].vy = sinf(angle) * frand(2,5);
        }
        int ci = rand() % palette_size;
        shapes[s].color.r = palette[ci][0];
        shapes[s].color.g = palette[ci][1];
        shapes[s].color.b = palette[ci][2];
        shapes[s].color.a = 255;
        pthread_mutex_init(&shapes[s].lock, NULL);
        pthread_create(&threads[s], NULL, simulate, &shapes[s]);
    }

    SDL_Event e;
    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN || e.type == SDL_MOUSEBUTTONDOWN) running = false;
        }

        SDL_SetRenderDrawColor(renderer, 3, 3, 6, 255);
        SDL_RenderClear(renderer);

        for (int s=0; s<NUM_SHAPES; s++) {
            pthread_mutex_lock(&shapes[s].lock);
            SDL_SetRenderDrawColor(renderer, shapes[s].color.r, shapes[s].color.g, shapes[s].color.b, 255);
            for (int i=0; i<POINTS_PER_SHAPE; i++) {
                int j = (i+1) % POINTS_PER_SHAPE;
                SDL_RenderDrawLine(renderer,
                    (int)shapes[s].points[i].x, (int)shapes[s].points[i].y,
                    (int)shapes[s].points[j].x, (int)shapes[s].points[j].y);
            }
            pthread_mutex_unlock(&shapes[s].lock);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16); // ~60 fps
    }

    for (int s=0; s<NUM_SHAPES; s++) {
        pthread_join(threads[s], NULL);
        pthread_mutex_destroy(&shapes[s].lock);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
