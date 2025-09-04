// mystify.c
// Screensaver secuencial + OpenMP con SDL2, CLI, FPS y benchmark (CSV).
// Compilación (Linux/Mac/WSL):
//   gcc mystify.c -o mystify -O2 -fopenmp `sdl2-config --cflags --libs`
// Compilación (MinGW):
//   gcc mystify.c -o mystify.exe -O2 -fopenmp -IC:\SDL2\include -LC:\SDL2\lib -lSDL2
//
// Ejemplos:
//   ./mystify --shapes 200 --points 6 --mode seq
//   ./mystify --shapes 200 --points 6 --mode omp
//   ./mystify --bench --secs 10 --shapes 600 --points 6 --w 1280 --h 720
//
// Notas:
// - Si compilas sin OpenMP, el modo "omp" caerá en secuencial con aviso.
// - El benchmark prueba hilos 1,2,4,8,... hasta omp_get_max_threads() y escribe bench.csv

#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef _OPENMP
  #include <omp.h>
#else
  // Sombra mínima para compilar sin OpenMP
  static inline int omp_get_max_threads(void){ return 1; }
  static inline int omp_get_thread_num(void){ return 0; }
#endif

// ---------- Config por defecto ----------
#define DEF_WIN_W 800
#define DEF_WIN_H 600
#define DEF_SHAPES 5
#define DEF_POINTS 6
#define DEF_SECS   0   // 0 = correr hasta cerrar.

// ---------- Tipos ----------
typedef struct { float x, y, vx, vy; } Point;

typedef struct {
    Point* points;       // tamaño = points_per_shape
    SDL_Color color;
} Shape;

typedef enum { MODE_SEQ=0, MODE_OMP=1 } RunMode;

typedef struct {
    int winW, winH;
    int num_shapes;
    int points_per_shape;
    int secs;         // duración; 0 = infinito
    RunMode mode;
    bool bench;
} Args;

// ---------- Utilidades ----------
static float frandf(float a, float b) {
    return a + (float)rand() / (float)RAND_MAX * (b - a);
}

static void print_help(const char* prog){
    printf(
      "Uso: %s [opciones]\n"
      "  --shapes N        Numero de figuras (1..50000). Default: %d\n"
      "  --points M        Puntos por figura (3..128). Default: %d\n"
      "  --w W             Ancho ventana (>= 320). Default: %d\n"
      "  --h H             Alto ventana  (>= 240). Default: %d\n"
      "  --secs T          Segundos a ejecutar (0=infinito). Default: %d\n"
      "  --mode seq|omp    Modo de ejecucion. Default: omp si disponible, si no seq\n"
      "  --bench           Corre benchmarks (CSV) variando hilos y modo\n"
      "  --help            Muestra esta ayuda\n",
      prog, DEF_SHAPES, DEF_POINTS, DEF_WIN_W, DEF_WIN_H, DEF_SECS
    );
}

static bool parse_int(const char* s, int* out){
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (!s || *s=='\0' || (end && *end!='\0')) return false;
    if (v < INT32_MIN || v > INT32_MAX) return false;
    *out = (int)v;
    return true;
}

static void parse_args(int argc, char** argv, Args* a){
    a->winW = DEF_WIN_W;
    a->winH = DEF_WIN_H;
    a->num_shapes = DEF_SHAPES;
    a->points_per_shape = DEF_POINTS;
    a->secs = DEF_SECS;
    a->bench = false;
#ifdef _OPENMP
    a->mode = MODE_OMP;
#else
    a->mode = MODE_SEQ;
#endif

    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i], "--help")){
            print_help(argv[0]); exit(0);
        } else if (!strcmp(argv[i], "--shapes") && i+1<argc){
            parse_int(argv[++i], &a->num_shapes);
        } else if (!strcmp(argv[i], "--points") && i+1<argc){
            parse_int(argv[++i], &a->points_per_shape);
        } else if (!strcmp(argv[i], "--w") && i+1<argc){
            parse_int(argv[++i], &a->winW);
        } else if (!strcmp(argv[i], "--h") && i+1<argc){
            parse_int(argv[++i], &a->winH);
        } else if (!strcmp(argv[i], "--secs") && i+1<argc){
            parse_int(argv[++i], &a->secs);
        } else if (!strcmp(argv[i], "--mode") && i+1<argc){
            const char* m = argv[++i];
            if (!strcmp(m,"seq")) a->mode = MODE_SEQ;
            else if (!strcmp(m,"omp")) a->mode = MODE_OMP;
            else { fprintf(stderr,"[ERR] --mode debe ser seq|omp\n"); exit(2); }
        } else if (!strcmp(argv[i], "--bench")){
            a->bench = true;
        } else {
            fprintf(stderr,"[ERR] Opcion no reconocida: %s\n", argv[i]);
            print_help(argv[0]); exit(2);
        }
    }

    // Validaciones
    if (a->num_shapes < 1 || a->num_shapes > 50000){
        fprintf(stderr,"[ERR] --shapes fuera de rango (1..50000)\n"); exit(2);
    }
    if (a->points_per_shape < 3 || a->points_per_shape > 128){
        fprintf(stderr,"[ERR] --points fuera de rango (3..128)\n"); exit(2);
    }
    if (a->winW < 320 || a->winH < 240){
        fprintf(stderr,"[ERR] --w/--h muy pequeños (min 320x240)\n"); exit(2);
    }
#ifndef _OPENMP
    if (a->mode==MODE_OMP){
        fprintf(stderr,"[WARN] OpenMP no disponible; usando modo secuencial.\n");
        a->mode = MODE_SEQ;
    }
#endif
}

// ---------- Datos / inicialización ----------
static const uint8_t PALETTE[][3] = {
  {0,170,255}, {255,0,170}, {255,255,0},
  {0,255,0}, {255,128,0}, {128,0,255}, {255,0,0}
};
static const int PALETTE_SIZE = sizeof(PALETTE)/sizeof(PALETTE[0]);

static void init_shapes(Shape* shapes, int num_shapes, int pts, int w, int h){
    for (int s=0; s<num_shapes; s++){
        shapes[s].points = (Point*)malloc(sizeof(Point)*pts);
        if (!shapes[s].points){ fprintf(stderr,"[ERR] sin memoria (points)\n"); exit(3); }
        for (int i=0;i<pts;i++){
            shapes[s].points[i].x  = frandf(50.0f, (float)w-50.0f);
            shapes[s].points[i].y  = frandf(50.0f, (float)h-50.0f);
            float ang = frandf(0.0f, (float)M_PI*2.0f);
            float spd = frandf(2.0f, 5.0f);
            shapes[s].points[i].vx = cosf(ang)*spd;
            shapes[s].points[i].vy = sinf(ang)*spd;
        }
        int ci = rand() % PALETTE_SIZE;
        shapes[s].color.r = PALETTE[ci][0];
        shapes[s].color.g = PALETTE[ci][1];
        shapes[s].color.b = PALETTE[ci][2];
        shapes[s].color.a = 255;
    }
}

static void free_shapes(Shape* shapes, int num_shapes){
    for (int s=0; s<num_shapes; s++){
        free(shapes[s].points);
        shapes[s].points = NULL;
    }
}

// ---------- Update (seq / omp) ----------
static inline void bounce(Point* p, int w, int h){
    p->x += p->vx;
    p->y += p->vy;
    if (p->x < 0.f){ p->x = 0.f; p->vx = -p->vx; }
    else if (p->x > (float)w){ p->x = (float)w; p->vx = -p->vx; }
    if (p->y < 0.f){ p->y = 0.f; p->vy = -p->vy; }
    else if (p->y > (float)h){ p->y = (float)h; p->vy = -p->vy; }
}

static void update_seq(Shape* shapes, int num_shapes, int pts, int w, int h){
    for (int s=0; s<num_shapes; s++){
        Point* P = shapes[s].points;
        for (int i=0;i<pts;i++){
            bounce(&P[i], w, h);
        }
    }
}

static void update_omp(Shape* shapes, int num_shapes, int pts, int w, int h){
#ifdef _OPENMP
    // Dos opciones: paralelizar por figura o por punto. Aquí por figura:
    #pragma omp parallel for schedule(static)
    for (int s=0; s<num_shapes; s++){
        Point* P = shapes[s].points;
        for (int i=0;i<pts;i++){
            bounce(&P[i], w, h);
        }
    }
#else
    // Fallback si no hay OpenMP
    update_seq(shapes, num_shapes, pts, w, h);
#endif
}

// ---------- Render ----------
static void render(SDL_Renderer* R, Shape* shapes, int num_shapes, int pts){
    // Fondo oscuro
    SDL_SetRenderDrawColor(R, 3,3,6,255);
    SDL_RenderClear(R);

    for (int s=0; s<num_shapes; s++){
        SDL_SetRenderDrawColor(R, shapes[s].color.r, shapes[s].color.g, shapes[s].color.b, 255);
        for (int i=0;i<pts;i++){
            int j = (i+1)%pts;
            SDL_RenderDrawLine(R,
                (int)shapes[s].points[i].x, (int)shapes[s].points[i].y,
                (int)shapes[s].points[j].x, (int)shapes[s].points[j].y
            );
        }
    }
    SDL_RenderPresent(R);
}

// ---------- Loop principal ----------
static double now_ms(void){
    return (double)SDL_GetTicks();
}

static double run_once(SDL_Window* W, SDL_Renderer* R, const Args* a){
    // Corre por a->secs (si >0) o hasta cerrar.
    const int target_ms_per_frame = 16; // ~60 fps
    Uint32 start_ms = SDL_GetTicks();
    Uint32 end_ms = (a->secs>0)? (start_ms + (Uint32)a->secs*1000u) : UINT32_MAX;

    Shape* shapes = (Shape*)calloc(a->num_shapes, sizeof(Shape));
    if (!shapes){ fprintf(stderr,"[ERR] sin memoria (shapes)\n"); exit(3); }
    init_shapes(shapes, a->num_shapes, a->points_per_shape, a->winW, a->winH);

    bool running = true;
    SDL_Event e;
    int frames = 0;
    double fps_timer = now_ms(), fps_acc_secs = 0.0;
    int fps_count = 0;
    double total_ms = 0.0;

    while (running){
        // Eventos
        while (SDL_PollEvent(&e)){
            if (e.type==SDL_QUIT) running=false;
            if (e.type==SDL_KEYDOWN || e.type==SDL_MOUSEBUTTONDOWN) running=false;
        }
        if (SDL_GetTicks() >= end_ms) running=false;

        double t0 = now_ms();
        // Update
        if (a->mode == MODE_SEQ) update_seq(shapes, a->num_shapes, a->points_per_shape, a->winW, a->winH);
        else                     update_omp(shapes, a->num_shapes, a->points_per_shape, a->winW, a->winH);

        // Render (main thread)
        render(R, shapes, a->num_shapes, a->points_per_shape);

        double t1 = now_ms();
        double dt = t1 - t0;
        total_ms += dt;
        frames++;

        // Limitar a ~60 FPS de manera simple (opcional)
        if (dt < target_ms_per_frame){
            SDL_Delay((Uint32)(target_ms_per_frame - dt));
        }

        // FPS cada ~500 ms en el título
        double now = now_ms();
        double elapsed = (now - fps_timer);
        fps_acc_secs += elapsed / 1000.0;
        fps_timer = now;
        fps_count++;

        if (fps_acc_secs >= 0.5){
            double fps = (double)frames / ((double)(now - (double)start_ms) / 1000.0);
            char title[128];
            snprintf(title, sizeof(title),
                "Mystify | %s | %d shapes x %d pts | FPS: %.1f",
                (a->mode==MODE_SEQ? "SEQ":"OMP"), a->num_shapes, a->points_per_shape, fps);
            SDL_SetWindowTitle(W, title);
            fps_acc_secs = 0.0;
            fps_count = 0;
        }
    }

    free_shapes(shapes, a->num_shapes);
    free(shapes);

    double avg_ms = (frames>0)? (total_ms/frames) : 0.0;
    return avg_ms; // devuelve ms por frame promedio (del trabajo lógico y render)
}

// ---------- Benchmark ----------
static void write_csv_header(FILE* f){
    fprintf(f, "mode,threads,shapes,points,width,height,secs,avg_ms_per_frame,fps,speedup,efficiency\n");
}

static void bench_all(SDL_Window* W, SDL_Renderer* R, Args a){
    // Prepara CSV
    FILE* f = fopen("bench.csv","w");
    if (!f){ fprintf(stderr,"[ERR] no se pudo crear bench.csv\n"); return; }
    write_csv_header(f);

    // Medimos secuencial primero como base
    a.mode = MODE_SEQ;
    a.secs = (a.secs>0? a.secs: 8); // por si no pusieron --secs
    printf("[BENCH] SEQ ...\n");
    double ms_seq = run_once(W,R,&a);
    double fps_seq = (ms_seq>0.0)? (1000.0/ms_seq) : 0.0;
    fprintf(f, "seq,%d,%d,%d,%d,%d,%d,%.6f,%.3f,%.3f,%.3f\n",
            1, a.num_shapes, a.points_per_shape, a.winW, a.winH, a.secs,
            ms_seq, fps_seq, 1.0, 1.0);

#ifdef _OPENMP
    int maxT = omp_get_max_threads();
    // probamos potencias de 2 hasta maxT
    for (int T=1; T<=maxT; T<<=1){
        printf("[BENCH] OMP threads=%d ...\n", T);
        // Fijar num threads para esta corrida
        omp_set_num_threads(T);
        Args b = a; b.mode = MODE_OMP;
        double ms_par = run_once(W,R,&b);
        double fps_par = (ms_par>0.0)? (1000.0/ms_par) : 0.0;
        double speedup = (ms_par>0.0)? (ms_seq/ms_par) : 0.0;
        double eff = (T>0)? (speedup/(double)T) : 0.0;

        fprintf(f, "omp,%d,%d,%d,%d,%d,%d,%.6f,%.3f,%.3f,%.3f\n",
                T, b.num_shapes, b.points_per_shape, b.winW, b.winH, b.secs,
                ms_par, fps_par, speedup, eff);
    }
#else
    printf("[BENCH] OpenMP no disponible; solo SEQ registrado.\n");
#endif

    fclose(f);
    printf("[BENCH] Listo: bench.csv\n");
}

// ---------- Main ----------
int main(int argc, char** argv){
    srand((unsigned)time(NULL));

    Args args;
    parse_args(argc, argv, &args);

    if (SDL_Init(SDL_INIT_VIDEO) != 0){
        fprintf(stderr,"[ERR] SDL_Init: %s\n", SDL_GetError()); return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Mystify", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        args.winW, args.winH, SDL_WINDOW_SHOWN
    );
    if (!window){ fprintf(stderr,"[ERR] SDL_CreateWindow: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    // SDL_RENDERER_PRESENTVSYNC no es obligatorio; usamos acelerado.
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer){ fprintf(stderr,"[ERR] SDL_CreateRenderer: %s\n", SDL_GetError()); SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    if (args.bench){
        bench_all(window, renderer, args);
    } else {
        (void)run_once(window, renderer, &args);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
