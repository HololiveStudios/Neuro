/*
 * neuro.c — Simulação do cérebro da Pestinha.
 *
 * Otimizações de memória em relação à versão anterior:
 *
 *   1. Matriz sináptica w[]  double[470×470] → float[470×290]
 *      Razão: neurônios sensoriais nunca são alvo de sinapse
 *      (a topologia nunca conecta nada → sensorial).  A dimensão
 *      "coluna" reduz de PESTINHA_NEURONS para NL = 290.
 *      Tipo double → float corta o restante pela metade.
 *      Resultado: 1 767 200 B → 545 200 B  (economia ≈ 1,16 MiB)
 *
 *   2. plastic_mask[]  double[30×4×72] → float[30×4×72]
 *      Resultado: 69 120 B → 34 560 B    (economia ≈ 33 KB)
 *
 *   3. spike[]  double[470] → uint8_t[470]
 *      0 ou 1 — não precisa de ponto flutuante.
 *      Resultado: 3 760 B → 470 B        (economia ≈ 3,2 KB)
 *
 *   Total Brain struct: ≈1,86 MiB → ≈595 KB  (redução de ~68%)
 *
 * A API pública em neuro.h não muda — Brain continua opaco.
 *
 * GPL-3.0-or-later
 */
#include "neuro.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "network_data.h"

/* ------------------------------------------------------------------ */
/* Constantes derivadas                                                */
/* ------------------------------------------------------------------ */

#define NS  (PESTINHA_CHANNELS * 6)   /* neurônios sensoriais = 180  */
#define NL  (PESTINHA_NEURONS - NS)   /* não-sensoriais       = 290  */
#define NPOOLS   (PESTINHA_CHANNELS + PESTINHA_INTERNEURONS + PESTINHA_MOTORS)
#define PLASTIC_POSTS 4
#define LEARN_MIN  (-0.05)
#define LEARN_MAX  ( 0.08)
#define REFRACTORY    3
#define NOISE_SIGMA   0.025

/* ------------------------------------------------------------------ */
/* Struct Brain                                                        */
/* ------------------------------------------------------------------ */

struct Brain {
    unsigned int rng;

    int         starts[NPOOLS];
    int         sizes[NPOOLS];
    int         kind[NPOOLS];
    const char *names[NPOOLS];

    /*
     * Matriz sináptica.
     * Indexação: w[pre * NL + (post - NS)]
     * Apenas alvos não-sensoriais são armazenados (colunas = NL).
     * Tipo float: metade do espaço de double, precisão suficiente.
     */
    float w[PESTINHA_NEURONS * NL];

    /* Mapeamento de plasticidade (pré-computado na criação) */
    int plastic_slot[NL];
    int plastic_offset[NL];
    int plastic_pool[PLASTIC_POSTS];

    /* Estado dos neurônios não-sensoriais */
    double v[NL];
    double adapt[NL];
    double adapt_b[NL];
    double adapt_decay[NL];
    double decay[NL];
    int    refractory[NL];

    /*
     * Spikes: 0 ou 1.  uint8_t em vez de double economiza 3,3 KB
     * e é mais semântico.
     */
    uint8_t spike[PESTINHA_NEURONS];

    double pre_gain[PESTINHA_NEURONS];  /* eficácia (sensoriais) */

    /* Estado sensorial */
    double sensory_rate[NS];
    double sensory_d[NS];
    double sensory_rec[NS];
    double efficacy[NS];
    double probability[NS];

    /* Níveis e elegibilidade por canal */
    double levels[PESTINHA_CHANNELS];
    double eligibility[PESTINHA_CHANNELS];

    /* Pesos plásticos aprendidos (double — acumulam com precisão) */
    double learned[PESTINHA_CHANNELS][PLASTIC_POSTS];

    /*
     * Máscara plástica aleatória.
     * float em vez de double: economiza 34,5 KB.
     */
    float plastic_mask[PESTINHA_CHANNELS][PLASTIC_POSTS][72];

    /* Taxas de disparo por pool */
    double pool_counts[NPOOLS];
    double pool_rates[NPOOLS];

    double time_ms;
};

/* ------------------------------------------------------------------ */
/* Alvos plásticos                                                     */
/* ------------------------------------------------------------------ */

static const char  *plastic_targets[PLASTIC_POSTS] =
    {"PLAYFUL", "CALM", "FRUSTRATION", "GLOOM"};
static const double plastic_sign[PLASTIC_POSTS] =
    {1.0, 1.0, -1.0, -1.0};

/* ------------------------------------------------------------------ */
/* RNG (xorshift32)                                                    */
/* ------------------------------------------------------------------ */

static unsigned int rand_u(unsigned int *state)
{
    unsigned int x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x ? x : 0x9e3779b9u;
    return *state;
}

static double urand(Brain *b)
{
    return ((rand_u(&b->rng) >> 8) + 1.0) / 16777217.0;
}

static double uniform(unsigned int *state)
{
    return ((rand_u(state) >> 8) + 1.0) / 16777217.0;
}

static double normal(Brain *b)
{
    double u = urand(b), v = urand(b);
    return sqrt(-2.0 * log(u)) * cos(6.283185307179586 * v);
}

static double normal_state(unsigned int *state)
{
    double u = uniform(state), v = uniform(state);
    return sqrt(-2.0 * log(u)) * cos(6.283185307179586 * v);
}

/* ------------------------------------------------------------------ */
/* Busca de índices                                                    */
/* ------------------------------------------------------------------ */

int brain_channel_index(const char *n)
{
    int i;
    for (i = 0; i < PESTINHA_CHANNELS; ++i)
        if (!strcmp(n, SENS[i].name)) return i;
    return -1;
}

int brain_motor_index(const char *n)
{
    int i;
    for (i = 0; i < PESTINHA_MOTORS; ++i)
        if (!strcmp(n, MOTOR[i].name)) return i;
    return -1;
}

static int find_pool(const Brain *b, const char *n)
{
    int i;
    for (i = 0; i < NPOOLS; ++i)
        if (!strcmp(n, b->names[i])) return i;
    return -1;
}

const char *brain_channel_name(int i)
{ return i >= 0 && i < PESTINHA_CHANNELS ? SENS[i].name  : ""; }
const char *brain_motor_name(int i)
{ return i >= 0 && i < PESTINHA_MOTORS   ? MOTOR[i].name : ""; }
const char *brain_interneuron_name(int i)
{ return i >= 0 && i < PESTINHA_INTERNEURONS ? INTER[i].name : ""; }

/* ------------------------------------------------------------------ */
/* Temperamento                                                        */
/* ------------------------------------------------------------------ */

static double temperament_gain(const char *temperament,
                               const char *pre, const char *post)
{
    static const char *jorge[][3] = {
        {"BOREDOM","CURIOUS","1.5"},{"BOREDOM","PLAYFUL","1.4"},
        {"LONELY","GLOOM",".6"},{"POKE","PLAYFUL","1.6"},
        {"CARESS","PLAYFUL","1.7"},{"CARESS","CALM","1.2"},
        {"DISMISS","FRUSTRATION","1.6"},{"RECUSA","FRUSTRATION","1.3"},
        {"TOY_TAKEN","GLOOM",".5"},{"TOY_TAKEN","FRUSTRATION","1.3"},
        {"NOVELTY","CURIOUS","1.3"}
    };
    static const char *bryan[][3] = {
        {"*","CURIOUS","1.3"},{"*","PLAYFUL","1.1"},
        {"DRAG","FRUSTRATION",".5"},{"TOY_TAKEN","GLOOM","1.5"},
        {"TOY_TAKEN","FRUSTRATION",".6"},{"TOY_NEAR","PLAYFUL","1.3"},
        {"DISMISS","FRUSTRATION","1.3"},{"IMPACT","FRUSTRATION","1.2"}
    };
    const char *(*table)[3] = NULL;
    int count = 0, i;
    if (!temperament) return 1.0;
    if (!strcmp(temperament, "jorge")) {
        table = jorge;
        count = (int)(sizeof jorge / sizeof jorge[0]);
    } else if (!strcmp(temperament, "bryan")) {
        table = bryan;
        count = (int)(sizeof bryan / sizeof bryan[0]);
    }
    for (i = 0; i < count; ++i)
        if (!strcmp(table[i][1], post) &&
            (!strcmp(table[i][0], pre) || !strcmp(table[i][0], "*")))
            return atof(table[i][2]);
    return 1.0;
}

/* ------------------------------------------------------------------ */
/* connect_pools — escreve na matriz retangular w[pre * NL + col]     */
/* ------------------------------------------------------------------ */

static void connect_pools(Brain *b, const char *pre, const char *post,
                          double weight, double density)
{
    int a = find_pool(b, pre), z = find_pool(b, post), i, j;
    double mean = exp(0.25 * 0.25 / 2.0);
    if (a < 0 || z < 0) return;

    for (i = 0; i < b->sizes[a]; ++i) {
        for (j = 0; j < b->sizes[z]; ++j) {
            if (urand(b) < density) {
                int from = b->starts[a] + i;
                int to   = b->starts[z] + j;
                /* Sensoriais não recebem sinapse na topologia */
                if (to < NS) continue;
                b->w[(size_t)from * NL + (to - NS)] +=
                    (float)(weight * (exp(0.25 * normal(b)) / mean));
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* brain_create                                                        */
/* ------------------------------------------------------------------ */

Brain *brain_create(unsigned int seed, const char *temperament)
{
    Brain *b = calloc(1, sizeof(*b));
    unsigned int plastic_rng_state;
    int i, j, cursor = 0;
    double mean = exp(0.25 * 0.25 / 2.0);
    if (!b) return NULL;
    b->rng = seed ? seed : 7u;
    plastic_rng_state = (seed ? seed : 7u) + 1u;

    /* --- Pools sensoriais --- */
    for (i = 0; i < PESTINHA_CHANNELS; ++i) {
        int n = SENS[i].n;
        b->starts[i] = cursor;
        b->sizes[i]  = n;
        b->kind[i]   = 0;
        b->names[i]  = SENS[i].name;
        cursor += n;
        for (j = 0; j < n; ++j) {
            int q = b->starts[i] + j;
            b->sensory_rate[q] = SENS[i].rate;
            b->sensory_d[q]    = SENS[i].d;
            b->sensory_rec[q]  = 0.001 / SENS[i].tau_rec;
            b->efficacy[q]     = 1.0;
        }
    }

    /* --- Interneurônios --- */
    for (i = 0; i < PESTINHA_INTERNEURONS; ++i) {
        int p = PESTINHA_CHANNELS + i;
        b->starts[p] = cursor;
        b->sizes[p]  = INTER[i].n;
        b->kind[p]   = !strcmp(INTER[i].kind, "inh") ? 2 : 1;
        b->names[p]  = INTER[i].name;
        cursor += b->sizes[p];
        for (j = 0; j < b->sizes[p]; ++j) {
            int q = b->starts[p] + j - NS;
            b->adapt_b[q]     = INTER[i].b;
            b->adapt_decay[q] = exp(-0.001 / INTER[i].bt);
            b->decay[q]       = exp(-1.0  / INTER[i].tau);
        }
    }

    /* --- Motores --- */
    for (i = 0; i < PESTINHA_MOTORS; ++i) {
        int p = PESTINHA_CHANNELS + PESTINHA_INTERNEURONS + i;
        b->starts[p] = cursor;
        b->sizes[p]  = MOTOR[i].n;
        b->kind[p]   = 3;
        b->names[p]  = MOTOR[i].name;
        cursor += b->sizes[p];
        for (j = 0; j < b->sizes[p]; ++j) {
            int q = b->starts[p] + j - NS;
            b->adapt_b[q]     = MOTOR[i].b;
            b->adapt_decay[q] = exp(-0.001 / MOTOR[i].bt);
            b->decay[q]       = exp(-1.0  / MOTOR[i].tau);
        }
    }

    if (cursor != PESTINHA_NEURONS) { free(b); return NULL; }

    for (i = 0; i < PESTINHA_NEURONS; ++i)
        b->pre_gain[i] = 1.0;

    /* --- Conexões da topologia --- */
    for (i = 0; i < (int)(sizeof CONNS / sizeof CONNS[0]); ++i) {
        double w = CONNS[i].weight;
        int ch = brain_channel_index(CONNS[i].pre);
        if (ch >= 0)
            w *= temperament_gain(temperament, CONNS[i].pre, CONNS[i].post);
        connect_pools(b, CONNS[i].pre, CONNS[i].post, w, CONNS[i].density);
    }

    /* --- Inibição lateral entre motores --- */
    for (i = 0; i < PESTINHA_MOTORS; ++i)
        for (j = 0; j < PESTINHA_MOTORS; ++j)
            if (i != j) {
                int di = (i >= 8), dj = (j >= 8);
                if (di == dj)
                    connect_pools(b,
                        MOTOR[i].name, MOTOR[j].name,
                        !strcmp(MOTOR[i].name, "STARTLE") ? -0.10 : -0.05,
                        0.7);
            }

    /* --- Mapa de plasticidade (pré-computado) --- */
    for (i = 0; i < NL; ++i) {
        b->plastic_slot[i]   = -1;
        b->plastic_offset[i] = 0;
    }
    for (j = 0; j < PLASTIC_POSTS; ++j) {
        int pool = find_pool(b, plastic_targets[j]);
        int k;
        b->plastic_pool[j] = pool;
        if (pool >= 0) {
            for (k = 0; k < b->sizes[pool]; ++k) {
                int q = b->starts[pool] + k - NS;
                if (q >= 0 && q < NL) {
                    b->plastic_slot[q]   = j;
                    b->plastic_offset[q] = k;
                }
            }
        }
    }

    /* --- Máscaras plásticas (float) --- */
    for (i = 0; i < PESTINHA_CHANNELS; ++i)
        for (j = 0; j < PLASTIC_POSTS; ++j) {
            int k;
            for (k = 0; k < 72; ++k)
                b->plastic_mask[i][j][k] = (uniform(&plastic_rng_state) < 0.5)
                    ? (float)(exp(0.25 * normal_state(&plastic_rng_state)) / mean)
                    : 0.0f;
        }

    return b;
}

void brain_destroy(Brain *b) { free(b); }

/* ------------------------------------------------------------------ */
/* brain_stimulate                                                     */
/* ------------------------------------------------------------------ */

void brain_stimulate(Brain *b, const double values[PESTINHA_CHANNELS])
{
    int c, j;
    if (!b) return;
    memset(b->probability, 0, sizeof b->probability);
    for (c = 0; c < PESTINHA_CHANNELS; ++c) {
        double x = fmax(0.0, fmin(1.0, values[c]));
        b->levels[c] = x;
        for (j = 0; j < b->sizes[c]; ++j) {
            int n = b->starts[c] + j;
            b->probability[n] = b->sensory_rate[n] * x * 0.001;
        }
    }
}

/* ------------------------------------------------------------------ */
/* substep — núcleo da simulação (1 ms)                               */
/* ------------------------------------------------------------------ */

static void substep(Brain *b)
{
    int i, j;
    double drive[NL];

    memset(drive, 0, sizeof drive);

    /*
     * Fase 1: neurônios sensoriais (i < NS).
     * Podem ter pre_gain != 1.0 (eficácia sináptica).
     * Verificam a plasticidade para alvos selecionados.
     */
    for (i = 0; i < NS; ++i) {
        if (b->spike[i]) {
            const float *row = b->w + (size_t)i * NL;
            double gain = b->pre_gain[i];
            int sensory_channel = i / 6;
            int sensory_local   = i % 6;

            for (j = 0; j < NL; ++j) {
                double wij = (double)row[j];

                if (b->plastic_slot[j] >= 0) {
                    int    slot        = b->plastic_slot[j];
                    int    target_size = b->sizes[b->plastic_pool[slot]];
                    int    mask_index  = sensory_local * target_size
                                      + b->plastic_offset[j];
                    wij += b->learned[sensory_channel][slot]
                         * (double)b->plastic_mask[sensory_channel][slot][mask_index];
                }

                drive[j] += gain * wij;
            }
        }
    }

    /*
     * Fase 2: neurônios não-sensoriais (i >= NS).
     * pre_gain é sempre 1.0 — sem multiplicação extra.
     * Sem plasticidade — loop mais curto.
     */
    for (i = NS; i < PESTINHA_NEURONS; ++i) {
        if (b->spike[i]) {
            const float *row = b->w + (size_t)i * NL;
            for (j = 0; j < NL; ++j)
                drive[j] += (double)row[j];
        }
    }

    /* --- Dinâmica de membrana e disparo --- */
    for (i = 0; i < NL; ++i) {
        b->v[i] = b->v[i] * b->decay[i]
                + drive[i]
                + NOISE_SIGMA * normal(b);

        if (b->refractory[i] > 0) {
            b->v[i] = 0.0;
            --b->refractory[i];
        }

        if (b->v[i] < -0.5) b->v[i] = -0.5;

        b->adapt[i] *= b->adapt_decay[i];

        if (b->v[i] >= 1.0 + b->adapt[i]) {
            b->v[i]          = 0.0;
            b->refractory[i] = REFRACTORY;
            b->spike[NS + i] = 1;
            b->adapt[i]     += b->adapt_b[i];
        } else {
            b->spike[NS + i] = 0;
        }
    }

    /* --- Spikes sensoriais e eficácia --- */
    for (i = 0; i < NS; ++i) {
        b->spike[i] = (urand(b) < b->probability[i]) ? 1 : 0;

        if (b->spike[i])
            b->efficacy[i] *= 1.0 - b->sensory_d[i];

        b->efficacy[i] += (1.0 - b->efficacy[i]) * b->sensory_rec[i];
        b->pre_gain[i]  = b->efficacy[i];
    }

    /* --- Contadores de disparo por pool --- */
    for (i = 0; i < NPOOLS; ++i) {
        int k;
        for (k = 0; k < b->sizes[i]; ++k)
            b->pool_counts[i] += b->spike[b->starts[i] + k];
    }
}

/* ------------------------------------------------------------------ */
/* brain_step                                                          */
/* ------------------------------------------------------------------ */

void brain_step(Brain *b, double milliseconds)
{
    int n, i;
    if (!b) return;
    n = (int)fmax(1.0, fmin(400.0, round(milliseconds)));
    for (i = 0; i < n; ++i) substep(b);
    for (i = 0; i < NPOOLS; ++i) {
        double r     = b->pool_counts[i] / (n * 0.001 * b->sizes[i]);
        double alpha = 1.0 - exp(-(n * 0.001) / 0.5);
        b->pool_rates[i] += alpha * (r - b->pool_rates[i]);
        b->pool_counts[i] = 0.0;
    }
    b->time_ms += n;
}

/* ------------------------------------------------------------------ */
/* Leituras                                                            */
/* ------------------------------------------------------------------ */

double brain_motor(const Brain *b, int i)
{
    return b && i >= 0 && i < PESTINHA_MOTORS
           ? b->pool_rates[PESTINHA_CHANNELS + PESTINHA_INTERNEURONS + i]
           : 0.0;
}

double brain_interneuron(const Brain *b, int i)
{
    return b && i >= 0 && i < PESTINHA_INTERNEURONS
           ? b->pool_rates[PESTINHA_CHANNELS + i]
           : 0.0;
}

double brain_efficacy(const Brain *b, int c)
{
    int j;
    double sum = 0.0;
    if (!b || c < 0 || c >= PESTINHA_CHANNELS) return 0.0;
    for (j = 0; j < b->sizes[c]; ++j)
        sum += b->efficacy[b->starts[c] + j];
    return sum / b->sizes[c];
}

/* ------------------------------------------------------------------ */
/* Aprendizagem                                                        */
/* ------------------------------------------------------------------ */

void brain_learn(Brain *b, double reward, double seconds, double rate)
{
    int i, j;
    double k;
    if (!b) return;

    k = 1.0 - exp(-seconds / 4.0);
    for (i = 0; i < PESTINHA_CHANNELS; ++i)
        b->eligibility[i] += (b->levels[i] - b->eligibility[i]) * k;

    if (reward == 0.0) return;

    for (i = 0; i < PESTINHA_CHANNELS; ++i)
        for (j = 0; j < PLASTIC_POSTS; ++j)
            b->learned[i][j] = fmax(LEARN_MIN, fmin(LEARN_MAX,
                b->learned[i][j]
                + rate * reward * seconds
                * b->eligibility[i] * plastic_sign[j]));
}

/* ------------------------------------------------------------------ */
/* Utilitários                                                         */
/* ------------------------------------------------------------------ */

void brain_reset_habituation(Brain *b)
{
    int i;
    if (!b) return;
    for (i = 0; i < NS; ++i)
        b->efficacy[i] = b->pre_gain[i] = 1.0;
}

/* ------------------------------------------------------------------ */
/* Persistência (formato inalterado — apenas efficacy e learned)      */
/* ------------------------------------------------------------------ */

void brain_save(const Brain *b, const char *path)
{
    FILE *f;
    int i, j;
    if (!b || !path || (f = fopen(path, "w")) == NULL) return;
    fprintf(f, "PESTINHA_BRAIN 1 %u\n", b->rng);
    for (i = 0; i < NS; ++i)
        fprintf(f, "%.9g%c", b->efficacy[i], (i + 1) % 12 ? ' ' : '\n');
    for (i = 0; i < PESTINHA_CHANNELS; ++i)
        for (j = 0; j < PLASTIC_POSTS; ++j)
            fprintf(f, "%.9g%c", b->learned[i][j],
                    j + 1 == PLASTIC_POSTS ? '\n' : ' ');
    fclose(f);
}

int brain_load(Brain *b, const char *path)
{
    FILE *f;
    char tag[32];
    int ver, i, j;
    unsigned int rng;
    double eff[NS], learn[PESTINHA_CHANNELS][PLASTIC_POSTS];

    if (!b || !path || (f = fopen(path, "r")) == NULL) return 0;
    if (fscanf(f, "%31s %d %u", tag, &ver, &rng) != 3 ||
        strcmp(tag, "PESTINHA_BRAIN") || ver != 1) {
        fclose(f); return 0;
    }
    for (i = 0; i < NS; ++i)
        if (fscanf(f, "%lf", &eff[i]) != 1 || eff[i] < 0 || eff[i] > 1) {
            fclose(f); return 0;
        }
    for (i = 0; i < PESTINHA_CHANNELS; ++i)
        for (j = 0; j < PLASTIC_POSTS; ++j)
            if (fscanf(f, "%lf", &learn[i][j]) != 1 ||
                learn[i][j] < LEARN_MIN || learn[i][j] > LEARN_MAX) {
                fclose(f); return 0;
            }
    fclose(f);
    memcpy(b->efficacy, eff, sizeof eff);
    memcpy(b->learned,  learn, sizeof learn);
    b->rng = rng;
    for (i = 0; i < NS; ++i)
        b->pre_gain[i] = b->efficacy[i];
    return 1;
}
