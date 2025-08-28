import os
import sys
import math
import time
import random
import multiprocessing as mp
from dataclasses import dataclass

# =============================================================
# Screensaver estilo "Mystify" (Win95-like) con paralelismo
# - Render con Pygame en el proceso principal
# - Simulación de múltiples figuras en procesos separados
# - Salida con cualquier movimiento de mouse o teclado
# =============================================================

try:
    import pygame
except ImportError:
    print("Este script requiere pygame. Instálalo con: pip install pygame")
    sys.exit(1)

# ------------------ Configuración ------------------
NUM_SHAPES = 6          # cantidad de figuras "mystify"
POINTS_PER_SHAPE = 6    # puntos por figura (polilínea cerrada)
SIM_FPS = 45            # FPS de simulación en procesos
RENDER_FPS = 60         # FPS de render en el proceso principal
LINE_THICKNESS = 2      # grosor de línea
AA_LINES = True         # usar líneas anti-aliased

# paleta clásica (Win95 vibes) + algunos tonos
PALETTE = [
    (0, 170, 255),   # cian
    (255, 0, 170),   # magenta
    (255, 255, 0),   # amarillo
    (0, 255, 0),     # verde
    (255, 128, 0),   # naranja
    (128, 0, 255),   # violeta
    (255, 0, 0),     # rojo
    (0, 255, 255),   # turquesa
]

@dataclass
class ShapeState:
    points: list  # list[(x, y)]
    vels: list    # list[(vx, vy)]
    color_idx: int


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def create_random_shape(w, h, n_points, rng):
    # puntos dentro de un margen para evitar "pegado" a los bordes
    margin = 80
    pts = [(rng.uniform(margin, w - margin), rng.uniform(margin, h - margin)) for _ in range(n_points)]
    vels = []
    for _ in range(n_points):
        ang = rng.uniform(0, 2 * math.pi)
        speed = rng.uniform(80, 200)  # px/seg
        vels.append((math.cos(ang) * speed, math.sin(ang) * speed))
    color_idx = rng.randrange(len(PALETTE))
    return ShapeState(pts, vels, color_idx)


def bounce_point(x, y, vx, vy, w, h):
    # rebote contra bordes
    if x < 0:
        x, vx = 0, abs(vx)
    elif x > w:
        x, vx = w, -abs(vx)
    if y < 0:
        y, vy = 0, abs(vy)
    elif y > h:
        y, vy = h, -abs(vy)
    return x, y, vx, vy


def simulate_shape(shape_id, screen_size, n_points, out_queue, stop_event, seed, sim_fps):
    rng = random.Random(seed)
    w, h = screen_size
    state = create_random_shape(w, h, n_points, rng)

    # dither inicial para que no arranquen sincronizados
    time.sleep(rng.uniform(0.0, 0.5))

    prev_time = time.perf_counter()
    dt_target = 1.0 / sim_fps

    # ocasionalmente variar color
    color_timer = 0.0

    while not stop_event.is_set():
        now = time.perf_counter()
        dt = now - prev_time
        if dt < dt_target:
            time.sleep(dt_target - dt)
            now = time.perf_counter()
            dt = now - prev_time
        prev_time = now

        pts = []
        vels = []
        for (x, y), (vx, vy) in zip(state.points, state.vels):
            x += vx * dt
            y += vy * dt
            x, y, vx, vy = bounce_point(x, y, vx, vy, w, h)
            pts.append((x, y))
            vels.append((vx, vy))

        state.points = pts
        state.vels = vels
        color_timer += dt
        if color_timer > 3.5:  # cambia color cada ~3.5s
            state.color_idx = (state.color_idx + 1 + rng.randrange(2)) % len(PALETTE)
            color_timer = 0.0

        # enviar último estado (modo "losers overwrite" para no bloquear)
        try:
            # limpiar mensajes viejos para este shape si hay demasiados
            # (evita backlog si el render va más lento)
            if out_queue.qsize() > 4:
                # no hay API para filtrar por shape_id; simplemente soltamos uno
                pass
            out_queue.put_nowait((shape_id, state.points, state.color_idx))
        except Exception:
            # si la cola está llena, ignoramos este frame
            pass

    # fin del proceso


def run_screensaver():
    # Pygame debe correr en el proceso principal
    os.environ["SDL_VIDEO_CENTERED"] = "1"
    pygame.init()

    flags = pygame.FULLSCREEN | pygame.HWSURFACE | pygame.DOUBLEBUF
    screen = pygame.display.set_mode((0, 0), flags)
    pygame.mouse.set_visible(False)
    info = pygame.display.Info()
    W, H = info.current_w, info.current_h

    clock = pygame.time.Clock()

    # Comunicación con procesos trabajadores
    ctx = mp.get_context("spawn")  # compatible con Windows
    stop_event = ctx.Event()
    out_queue = ctx.Queue(maxsize=64)

    # iniciar workers
    workers = []
    for sid in range(NUM_SHAPES):
        p = ctx.Process(
            target=simulate_shape,
            args=(sid, (W, H), POINTS_PER_SHAPE, out_queue, stop_event, random.randrange(10**9), SIM_FPS),
            daemon=True,
        )
        p.start()
        workers.append(p)

    # estado visible actual de cada shape
    shapes_points = {sid: None for sid in range(NUM_SHAPES)}
    shapes_color = {sid: 0 for sid in range(NUM_SHAPES)}

    # fondo casi negro como los CRT
    BG = (3, 3, 6)

    # trail/fade: dibujamos un rect semitransparente para efecto de estela
    fade_surface = pygame.Surface((W, H), pygame.SRCALPHA)
    fade_surface.fill((0, 0, 0, 18))  # alpha bajo para estela suave

    # título flotante sutil
    font = pygame.font.SysFont("Verdana", 18, bold=False)
    title_text = font.render("Mystify-ish (Python)", True, (120, 120, 160))

    # track del mouse para salir si se mueve
    mouse_origin = pygame.mouse.get_pos()

    running = True
    while running:
        # manejo de eventos (salida con ESC, click, o movimiento de mouse)
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                running = False
            elif event.type == pygame.MOUSEBUTTONDOWN:
                running = False
            elif event.type == pygame.MOUSEMOTION:
                # salir si el mouse se movió más de 5 px desde el inicio
                mx, my = event.pos
                if abs(mx - mouse_origin[0]) > 5 or abs(my - mouse_origin[1]) > 5:
                    running = False

        # vaciar cola con los últimos estados disponibles
        try:
            # leer todo lo disponible sin bloquear
            while True:
                sid, pts, color_idx = out_queue.get_nowait()
                shapes_points[sid] = pts
                shapes_color[sid] = color_idx
        except Exception:
            pass

        # dibujar con efecto de estela
        screen.fill(BG)
        screen.blit(fade_surface, (0, 0))

        for sid in range(NUM_SHAPES):
            pts = shapes_points[sid]
            if not pts:
                continue
            color = PALETTE[shapes_color[sid] % len(PALETTE)]
            # cerrar la polilínea (último -> primero)
            closed_pts = pts + [pts[0]]
            if AA_LINES:
                pygame.draw.aalines(screen, color, False, closed_pts,)
                if LINE_THICKNESS > 1:
                    # extra pass para grosor (aalines no acepta width)
                    pygame.draw.lines(screen, color, False, closed_pts, LINE_THICKNESS)
            else:
                pygame.draw.lines(screen, color, False, closed_pts, LINE_THICKNESS)

        # texto sutil
        screen.blit(title_text, (20, H - 40))

        pygame.display.flip()
        clock.tick(RENDER_FPS)

    # detener workers
    stop_event.set()
    for p in workers:
        p.join(timeout=0.5)
        if p.is_alive():
            p.terminate()

    pygame.quit()


if __name__ == "__main__":
    try:
        run_screensaver()
    except KeyboardInterrupt:
        pass
