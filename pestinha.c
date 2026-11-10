#define _POSIX_C_SOURCE 199309L
#include "neuro.h"
#include "display_xcb.h"
#include "conditioning.h"
#include "events.h"
#include "world.h"
#include "homeostasis.h"
#include "behavior.h"
#include "memory.h"
#include "runtime_types.h"
#include "body.h"
#include "fx.h"
#include "ui.h"
#include "sensors.h"
#include "xutil.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/xcb.h>

#define DT 0.01
#define BRAIN_FILE_PREFIX "pestinha-"

static double inputs[PESTINHA_CHANNELS], pulse[PESTINHA_CHANNELS];
static double simtime = 0;
static ConditioningState conditioning;
static Goal active_goal = {0};
static WorldSnapshot snapshot = {0};
static EventType transition_event = EVENT_NONE;
static int transition_context = 0;
static FaceState face = {0, -1, 0};
static double dismiss_history[3]; static int dismiss_count = 0;
static Drives drives = {1.0, .6, 0.0}; static Emotion emotion = {0, 0, 0, 0, 0};
static Behavior behavior = {0}; static EpisodeMemory episodic;
static const char *pet_name = "bryan";
static char brain_file[64], state_file[64], memory_file[64];
static Brain *brain;
static int running = 1;

static double clampd(double x, double a, double b) { return fmax(a, fmin(b, x)); }
static void setch(const char *n, double x) { int i = brain_channel_index(n); if (i >= 0) inputs[i] = clampd(x, 0, 1); }
static void kick(const char *n, double x) { int i = brain_channel_index(n); if (i >= 0) pulse[i] = clampd(pulse[i] + x, 0, 1); }

static void simulate(double seconds, int engaged, int moving, int visited)
{
    int n = (int)(seconds / DT), i, c;
    for (i = 0; i < n; i++) {
        int previous_context = snapshot.context_id;
        simtime += DT;
        for (c = 0; c < PESTINHA_CHANNELS; c++) inputs[c] = clampd(inputs[c] * .92 + pulse[c], 0, 1);
        for (c = 0; c < PESTINHA_CHANNELS; c++) pulse[c] *= exp(-DT / (c == 10 ? .35 : 2));
        setch("SILENCE", 1);
        setch("ANTICIPATE", fmax(conditioning.press_value, conditioning.approach_value));
        setch("ANTECIPA_PLACA", conditioning.plaque_value);
        setch("PLACA", behavior.corner ? 1 : 0);
        setch("VISITA", behavior.corner && visited ? 1 : 0);
        setch("PEDIDO", simtime < behavior.request_until ? 1 : 0);
        setch("PRESO", behavior.corner ? 1 : 0);
        setch("LEMBRA_FUGA", behavior.escape_memory);
        setch("RECEIO", behavior.fear);
        setch("RANCOR", behavior.rancor);
        brain_stimulate(brain, inputs);
        brain_step(brain, 10);
        brain_learn(brain, 0, DT, .03);
        homeostasis_update(&drives, &behavior, DT, moving, emotion.arousal, 0, engaged, visited, inputs);
        behavior_update_emotion(&emotion, &behavior, &face, brain, simtime, DT);
        behavior_update(&behavior, &drives, &conditioning, brain, inputs, simtime, DT);
        conditioning_update(&conditioning, simtime);
        snapshot = world_snapshot(simtime, &drives, &emotion, &behavior, active_goal.type);
        goal_update(&active_goal, &snapshot);
        snapshot.current_goal = active_goal.type;
        episodic.goal = active_goal;
        if (transition_event != EVENT_NONE) {
            world_model_observe(&episodic.world_model, transition_context, transition_event, snapshot.context_id);
            transition_event = EVENT_NONE;
        } else if (snapshot.context_id != previous_context) {
            Event internal = event_internal(EVENT_DRIVE_CHANGED, simtime, 1.0, 0.8);
            world_model_observe(&episodic.world_model, previous_context, internal.type, snapshot.context_id);
        }
        memory_tick(&episodic, simtime);
        if (behavior.asleep || drives.stimulation < 0.25) memory_consolidate(&episodic, simtime);
    }
}

static void handle_event(const Event *event)
{
    double attention = event_attention(event, &snapshot, &episodic.world_model);
    transition_event = event->type;
    transition_context = snapshot.context_id;
    if (event->type != EVENT_QUIT) behavior.last_interaction = event->timestamp;
    switch (event->type) {
    case EVENT_CARESS:
        kick("CARESS", event->intensity); homeostasis_event(&drives, "caress");
        brain_learn(brain, 1, .8, .03); face_force(&face, simtime, 9, 1);
        conditioning_outcome(&conditioning, 0, event->timestamp);
        if (attention > 0.35) memory_resolve(&episodic, 0, 0.6 * event->confidence, event->timestamp, NULL);
        break;
    case EVENT_POKE:
        conditioning_start(&conditioning, 0, event->timestamp);
        conditioning_end(&conditioning, 0, event->timestamp);
        kick("POKE", event->intensity); homeostasis_event(&drives, "poke");
        conditioning_outcome(&conditioning, 0, event->timestamp); face_force(&face, simtime, 3, 1); break;
    case EVENT_WORK:
        behavior.request_until = event->timestamp + 10; setch("WORK", 1); break;
    case EVENT_DISMISS:
        kick("DISMISS", event->intensity); kick("RECUSA", event->intensity);
        homeostasis_event(&drives, "dismiss"); brain_learn(brain, -1, 1, .03);
        memory_resolve(&episodic, 0, -.8 * event->confidence, event->timestamp, NULL);
        dismiss_history[dismiss_count % 3] = event->timestamp; dismiss_count++;
        if (dismiss_count >= 3 && event->timestamp - dismiss_history[dismiss_count % 3] < 15) face_force(&face, simtime, 7, 2);
        if (behavior.corner) behavior.fear = clampd(behavior.fear + .1, 0, 1);
        break;
    case EVENT_CORNER:
        brain_learn(brain, -1, 1, .03);
        behavior.corner = 1; behavior.corner_time = 0; behavior.escape_will = 0;
        setch("PLACA", 1); conditioning_start(&conditioning, 2, event->timestamp);
        conditioning_end(&conditioning, 2, event->timestamp);
        conditioning_outcome(&conditioning, 2, event->timestamp);
        memory_resolve(&episodic, 0, -0.5 * event->confidence, event->timestamp, NULL); break;
    case EVENT_VISIT:
        if (behavior.corner) { setch("VISITA", 1); kick("CARESS", event->intensity); homeostasis_event(&drives, "caress"); } break;
    case EVENT_RELEASE:
        if (behavior.corner) { behavior.corner = 0; behavior.escape_will = 0; behavior.fear = clampd(behavior.fear + .3, 0, 1); memory_resolve(&episodic, 0, .2 * event->confidence, event->timestamp, NULL); } break;
    case EVENT_MISCHIEF: {
        int context, goal, allowed, action;
        if (episodic.pending_valid[0]) break;
        context = memory_context(&snapshot);
        goal = active_goal.type;
        allowed = (behavior.asleep || behavior.corner) ? (1 << ACTION_WAIT) : 0x1f;
        action = memory_choose(&episodic, context, goal, allowed);
        if (action >= 0) {
            ActionProposal proposal;
            proposal.action = action;
            proposal.expected_value = episodic.value[context][goal][action];
            proposal.confidence = episodic.count[context][goal][action] /
                                  (episodic.count[context][goal][action] + 3.0);
            proposal.cost = 0.1;
            proposal.allowed = 1;
            if (proposal.allowed) {
                if (action != ACTION_WAIT) homeostasis_event(&drives, "mischief");
                memory_begin(&episodic, 0, context, goal, proposal.action,
                             proposal.expected_value, event->timestamp);
                episodic.pending[0].confidence = proposal.confidence;
                switch (action) {
                case ACTION_WANDER: kick("MOTION", .6); break;
                case ACTION_PLAY: kick("PLAY", .8); break;
                case ACTION_SETTLE: kick("SILENCE", .7); break;
                case ACTION_REQUEST: kick("PEDIDO", .8); break;
                default: break;
                }
            }
        }
        break;
    }
    case EVENT_THROW:
        kick("THROW", event->intensity); face_force(&face, simtime, 1, 1); break;
    case EVENT_QUIT:
        running = 0; break;
    default: break;
    }
}

static void save_state(void)
{
    FILE *f;
    char tmp[80];
    snprintf(tmp, sizeof(tmp), "%s.tmp", state_file);
    f = fopen(tmp, "w");
    if (f) {
        fprintf(f, "energy=%.4f\nstimulation=%.4f\ncontact=%.4f\nsimtime=%.2f\n",
                drives.energy, drives.stimulation, drives.contact, simtime);
        fclose(f);
        (void)rename(tmp, state_file);
    }
    brain_save(brain, brain_file);
    memory_save(&episodic, memory_file);
}

static void load_state(void)
{
    FILE *f = fopen(state_file, "r");
    if (f) {
        char line[64];
        while (fgets(line, sizeof(line), f)) {
            if (!strncmp(line, "energy=", 7)) drives.energy = atof(line + 7);
            else if (!strncmp(line, "stimulation=", 12)) drives.stimulation = atof(line + 12);
            else if (!strncmp(line, "contact=", 8)) drives.contact = atof(line + 8);
            else if (!strncmp(line, "social=", 7)) drives.contact = atof(line + 7);
            else if (!strncmp(line, "simtime=", 8)) simtime = atof(line + 8);
        }
        fclose(f);
    }
    memory_load(&episodic, memory_file);
}

static void demo(void)
{
    clock_t started = clock();
    int i;
    for (i = 0; i < 60; i++) {
        if (i == 5) handle_event(&(Event){EVENT_CARESS, simtime, 1.0, 1.0, 0});
        if (i == 10) handle_event(&(Event){EVENT_MISCHIEF, simtime, 1.0, 1.0, 0});
        if (i == 25) handle_event(&(Event){EVENT_DISMISS, simtime, 1.0, 1.0, 0});
        simulate(0.1, 0, 0, 0);
    }
    printf("Avaliação determinística concluída: tempo de CPU=%.4f s, "
           "tamanho da memória=%lu bytes, experiências resolvidas=%lu, "
           "erro médio de previsão=%.4f, avaliações do modelo=%lu\n",
           (double)(clock() - started) / (double)CLOCKS_PER_SEC,
           (unsigned long)sizeof(episodic), episodic.metrics.resolved_experiences,
           memory_mean_prediction_error(&episodic),
           episodic.world_model.prediction_evaluations);
}

int main(int argc, char **argv)
{
    int i, jorge = 0;
    const char *mode = "";
    struct timespec pause = {0, 33000000};
    PestinhaDisplay *display;
    Fx *fx = NULL;
    Ui *ui = NULL;
    Sensors *sensors = NULL;
    Body body;
    AiState ai;
    Area work_area = {0, 0, 1920, 1080};
    Platform platforms[16];
    int nplats = 0;
    int sensors_on = 1;
    double last_platform_scan = -10.0;
    int corner_sign_id = -1;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--pet") && i + 1 < argc) {
            jorge = !strcmp(argv[++i], "jorge");
        }
        else if (!strcmp(argv[i], "--demo") ||
                 !strcmp(argv[i], "--metrics") ||
                 !strcmp(argv[i], "--evaluate") ||
                 !strcmp(argv[i], "--help")) {
            mode = argv[i];
        }
    }

    if (!strcmp(mode, "--help")) {
        puts("Pestinha C: --pet jorge|bryan, --demo, --metrics, --evaluate, --help.");
        puts("No modo desktop:");
        puts("O pet anda livremente pela tela e pousa nas janelas ativas.");
        puts("Pode ser arrastado com o mouse e o clique direito abre o menu.");
        puts("O menu permite carinho, cutucar, trabalho, cantinho, visita e outras ações.");
        return 0;
    }

    pet_name = jorge ? "jorge" : "bryan";
    snprintf(brain_file, sizeof(brain_file), BRAIN_FILE_PREFIX "%s.brain", pet_name);
    snprintf(state_file, sizeof(state_file), BRAIN_FILE_PREFIX "%s.state", pet_name);
    snprintf(memory_file, sizeof(memory_file), BRAIN_FILE_PREFIX "%s.memory", pet_name);
    brain = brain_create(jorge ? 1729u : 7u, pet_name);
    if (!brain) {
        fputs("Falha ao criar a rede neural.\n", stderr);
        return 1;
    }
    memory_init(&episodic);
    conditioning_init(&conditioning);
    behavior.last_interaction = -1.0;
    snapshot = world_snapshot(simtime, &drives, &emotion, &behavior, GOAL_IDLE);
    goal_update(&active_goal, &snapshot);
    episodic.goal = active_goal;

    if (!strcmp(mode, "--demo") || !strcmp(mode, "--evaluate")) {
        demo();
        brain_destroy(brain);
        return 0;
    }
    if (strcmp(mode, "--faces")) {
        load_state();
        brain_load(brain, brain_file);
    }
    if (!strcmp(mode, "--metrics")) {
        printf("pet=%s experiences=%lu mean_prediction_error=%.4f "
               "world_prediction_error=%.4f action_switches=%lu "
               "goal=%d context=%d user_present=%d\n",
               pet_name, episodic.metrics.resolved_experiences,
               memory_mean_prediction_error(&episodic),
               episodic.world_model.prediction_evaluations ?
                   episodic.world_model.prediction_error /
                   (double)episodic.world_model.prediction_evaluations : 0.0,
               episodic.metrics.action_switches, active_goal.type,
               snapshot.context_id, snapshot.user_present);
        brain_destroy(brain);
        return 0;
    }

    display = display_open_desktop(jorge, 1);

    fx = fx_open();
    ui = ui_open();
    sensors = sensors_open();

    if (ui)
        ui_hotkey(ui);

    if (!display) {
        fputs("Não consegui abrir uma janela gráfica XCB. Confira o ambiente gráfico e a biblioteca libxcb.\n", stderr);
        brain_destroy(brain);
        return 1;
    }

    {
        int pw, ph;

        display_size(display, &pw, &ph);
        display_work_area(display, &work_area);

        body_init(&body, pw, ph, 0.35, 8.0);
        body_place(&body,
                   work_area.x + work_area.w / 2.0,
                   work_area.y + work_area.h - ph);

        ai_init(&ai, (unsigned)(time(NULL) ^ 0x5a5a));
    }

    while (running) {
            /* Modo Desktop Flutuante com Física, Sensores, Efeitos e UI */
            xcb_connection_t *conn = display_connection(display);
            xcb_generic_event_t *e;
            SensorReading sr = {0};
            MoveInputs in = {0};
            UiAction action = UI_NONE;

            /* 1. Atualiza plataformas de janelas a cada 0.5s */
            if (simtime - last_platform_scan > 0.5) {
                display_work_area(display, &work_area);
                nplats = display_platforms(display, body.h + 10, platforms, 16);
                last_platform_scan = simtime;
            }

            /* 2. Processa eventos de mouse e janela no pet */
            while (conn && (e = xcb_poll_for_event(conn))) {
                uint8_t t = e->response_type & ~0x80;
                if (t == XCB_CLIENT_MESSAGE || t == XCB_DESTROY_NOTIFY) {
                    running = 0;
                } else if (t == XCB_BUTTON_PRESS) {
                    xcb_button_press_event_t *bp = (xcb_button_press_event_t *)e;
                    if (bp->detail == 1) {
                        body_grab(&body, (double)bp->root_x, (double)bp->root_y, simtime);
                        handle_event(&(Event){EVENT_POKE, simtime, 0.6, 1.0, 0});
                    } else if (bp->detail == 3 && ui) {
                        ui_menu_open(ui, bp->root_x, bp->root_y);
                    }
                } else if (t == XCB_MOTION_NOTIFY) {
                    xcb_motion_notify_event_t *mn = (xcb_motion_notify_event_t *)e;
                    if (body.dragging) {
                        body_drag(&body, (double)mn->root_x, (double)mn->root_y, simtime);
                    }
                } else if (t == XCB_BUTTON_RELEASE) {
                    xcb_button_release_event_t *br = (xcb_button_release_event_t *)e;
                    if (br->detail == 1 && body.dragging) {
                        double throw_spd = 0.0;
                        body_release(&body, simtime, &throw_spd);
                        if (throw_spd > 600.0) {
                            handle_event(&(Event){EVENT_THROW, simtime, clampd(throw_spd / 2000.0, 0.2, 1.0), 1.0, 0});
                        }
                    }
                }
                free(e);
            }

            /* 3. Consulta sensores reais do sistema */
            if (sensors && sensors_on) {
                sensors_poll(sensors, simtime, &sr);
                setch("WORK", sr.work);
                setch("WEB", sr.web);
                setch("MOTION", sr.motion);
                setch("TYPING", sr.typing);
                setch("SOUND", sr.sound);
                setch("NOVELTY", sr.novelty);
                if (sr.idle_seconds > 45.0) setch("SILENCE", 1.0);

                in.pointer_valid = 1;
                in.pointer_x = sr.pointer_x;
                in.pointer_y = sr.pointer_y;
            }

            /* 4. Ações da UI (Menu de contexto / Diálogos / Atalho global) */
            if (ui && ui_poll(ui, &action)) {
                switch (action) {
                case UI_CARESS: handle_event(&(Event){EVENT_CARESS, simtime, 1.0, 1.0, 0}); break;
                case UI_POKE: handle_event(&(Event){EVENT_POKE, simtime, 1.0, 1.0, 0}); break;
                case UI_WORK: handle_event(&(Event){EVENT_WORK, simtime, 1.0, 1.0, 0}); break;
                case UI_DISMISS: handle_event(&(Event){EVENT_DISMISS, simtime, 1.0, 1.0, 0}); break;
                case UI_CORNER: handle_event(&(Event){EVENT_CORNER, simtime, 1.0, 1.0, 0}); break;
                case UI_VISIT: handle_event(&(Event){EVENT_VISIT, simtime, 1.0, 1.0, 0}); break;
                case UI_RELEASE: handle_event(&(Event){EVENT_RELEASE, simtime, 1.0, 1.0, 0}); break;
                case UI_MISCHIEF: handle_event(&(Event){EVENT_MISCHIEF, simtime, 1.0, 1.0, 0}); break;
                case UI_THROW: body_hop(&body, -850.0); break;
                case UI_BALL:
                    if (fx) {
                        int on = !fx_ball_enabled(fx);
                        fx_ball_enable(fx, on, body.x + 80, body.y);
                    }
                    break;
                case UI_SENSORS:
                case UI_SENSORS_ON: sensors_on = 1; break;
                case UI_SENSORS_OFF: sensors_on = 0; break;
                case UI_CLEAR_FX: if (fx) fx_clear(fx); break;
                case UI_QUIT: running = 0; break;
                default: break;
                }
            }

            /* 5. Simulação neural e comportamental */
            simulate(0.033, simtime < behavior.request_until, (fabs(body.vx) > 10.0), 0);

            /* 6. Atualização física do piloto e corpo */
            in.wander = brain_motor(brain, 2);
            in.cheer = brain_motor(brain, 3);
            in.startle = brain_motor(brain, 0);
            in.flee = brain_motor(brain, 9);
            in.canto = brain_motor(brain, 11);
            in.sitting = (brain_motor(brain, 8) > 2.5) || (simtime < behavior.request_until);
            in.asleep = behavior.asleep;
            in.corner = behavior.corner;
            in.strike = behavior.protest;

            ai_step(&ai, &body, &in, &work_area, simtime, 0.033);
            body_step(&body, 0.033, &work_area, platforms, nplats);
            display_move(display, (int)body.x, (int)body.y);

            /* 7. Efeitos visuais do mundo (FX) */
            if (fx) {
                /* Notebook do trabalho */
                fx_notebook(fx, in.sitting, (int)body.x, (int)body.y, body.w, body.h);

                /* Placa do cantinho do castigo */
                if (behavior.corner && corner_sign_id < 0) {
                    corner_sign_id = fx_poster(fx, work_area.x + work_area.w - 180,
                                               work_area.y + work_area.h - 90,
                                               "CANTINHO DO CASTIGO", "", FX_STYLE_SIGN, 0);
                } else if (!behavior.corner && corner_sign_id >= 0) {
                    fx_remove(fx, corner_sign_id);
                    corner_sign_id = -1;
                }

                /* Cartazes de protesto quando atinge greve */
                if (in.strike && fx_poster_count(fx) < 2 && (rand() % 90 == 0)) {
                    fx_poster(fx, (int)body.x + (rand() % 60 - 30),
                              (int)body.y - 70, "GREVE",
                              "Em greve até haver carinho!", FX_STYLE_PROTEST, 12.0);
                }

                /* Fogos de artifício quando está muito alegre */
                if (in.cheer > 8.0 && fx_particle_count(fx) < 150 && (rand() % 45 == 0)) {
                    fx_fireworks(fx, body.x + body.w / 2.0, body.y, 0);
                }

                /* Bolinha de brincar e chutes */
                if (fx_ball_enabled(fx)) {
                    double bx, by;
                    fx_ball_center(fx, &bx, &by);
                    double dist = hypot(bx - (body.x + body.w / 2.0), by - (body.y + body.h / 2.0));
                    if (dist < 45.0) {
                        fx_ball_kick(fx, (bx > body.x ? 350.0 : -350.0), -380.0);
                        kick("PLAY", 0.8);
                        kick("TOY_NEAR", 1.0);
                    }
                }

                fx_update(fx, 0.033, &work_area, platforms, nplats);
            }

            display_draw(display, face.current_face, drives.energy, brain_motor(brain, 3),
                         brain_motor(brain, 6), brain_motor(brain, 7), behavior.corner,
                         behavior.asleep, simtime);

        (void)nanosleep(&pause, NULL);
    }

    save_state();
    if (fx) fx_close(fx);
    if (ui) ui_close(ui);
    if (sensors) sensors_close(sensors);
    display_close(display);
    brain_destroy(brain);
    return 0;
}
