#ifndef PESTINHA_NEURO_H
#define PESTINHA_NEURO_H

#define PESTINHA_CHANNELS 30
#define PESTINHA_INTERNEURONS 10
#define PESTINHA_MOTORS 15
#define PESTINHA_NEURONS 470

typedef struct Brain Brain;
Brain *brain_create(unsigned int seed, const char *temperament);
void brain_destroy(Brain *brain);
void brain_stimulate(Brain *brain, const double values[PESTINHA_CHANNELS]);
void brain_step(Brain *brain, double milliseconds);
double brain_motor(const Brain *brain, int motor_index);
double brain_interneuron(const Brain *brain, int index);
double brain_efficacy(const Brain *brain, int channel_index);
void brain_learn(Brain *brain, double reward, double seconds, double rate);
void brain_reset_habituation(Brain *brain);
void brain_save(const Brain *brain, const char *path);
int brain_load(Brain *brain, const char *path);
const char *brain_channel_name(int index);
const char *brain_motor_name(int index);
const char *brain_interneuron_name(int index);
int brain_channel_index(const char *name);
int brain_motor_index(const char *name);

#endif
