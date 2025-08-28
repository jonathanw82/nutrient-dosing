#ifndef DOSA_H
#define DOSA_H
#include "module.h"


class Dosa_Cls: public Module_Cls {

  public:

    // pins
    short mixture_valve_pin;
    short ph_valve_pin;
    short nutrient_A_valve_pin;
    short nutrient_B_valve_pin;
    float dose_amount_l;
    short emergency_stop_pin;
    short lockout_led_pin;

    Dosa_Cls();
    void init();
    void main();
    void publish_status();
    bool process_message(char* topic, char* payload);
    void get_commission_path_str(char*);

    // state runners
    void run_uncommissioned_state();

  private:

    float flow_rate;
    float ratio_of_A_to_B;
    float prev_ratio_of_A_to_B;
    long ph_dose_time_s;
    bool needs_to_dose_ec;
    bool needs_to_dose_ph;
    bool dose_lockout;
    bool prev_dose_lockout;
    bool mixture_valve_pin_state;
    bool prev_mixture_valve_pin_state;
    bool ph_valve_pin_state;
    bool prev_ph_valve_pin_state;
    bool nutrient_A_valve_pin_state;
    bool prev_nutrient_A_valve_pin_state;
    bool nutrient_B_valve_pin_state;
    bool prev_nutrient_B_valve_pin_state;

    // State Machines
    enum ph_state {ph_dose_start, ph_dose_run_timer, ph_dose_idle, ph_dose_end};
    ph_state ph_dose_state;

    enum ec_state {ec_dose_start, ec_dose_run_timer, ec_dose_idle, ec_dose_end};
    ec_state ec_A_dose_state; 
    ec_state ec_B_dose_state;

    enum lockout_state {none_lockout, safety_dose_lockout, safety_timer_lockout_EC, safety_timer_lockout_PH, emergency_stop_button};
    lockout_state lockout_type;

    float dose_ph_timer;
    float dose_A_timer;
    float dose_B_timer;
    float dose_A_time_s;
    float prev_dose_A_time_s;
    float dose_B_time_s;
    float prev_dose_B_time_s;
    bool calculate_ec_ratio();
    bool dose_nutrient_a();
    bool dose_nutrient_b();
    bool dose_ph();
    bool mixture_state;
    bool current_mixture_state;
    bool manage_mixture();
    bool pump_state;
    bool current_pump_state;
    bool lockout_state_control;
    bool prev_lockout_state_control;
    bool lockout_state_ec;
    bool prev_lockout_state_ec;
    bool lockout_state_ph;
    bool prev_lockout_state_ph;
    bool lockout_state_emergency_stop;    
    bool emergency_stop_state;
    bool prev_emergency_stop_state;
    bool current_emergency_stop_state;

    void manage_lockout();
    void manage_emergency_stop(); 
    void check_error_state();
    long safety_timout_limit_s;
    bool check_ec_B_safety_timer();
    bool check_ec_A_safety_timer();
    bool check_ph_safety_timer();

    // MQTT publish functions
    void pub_stat_ph();
    void pub_doseing_times();
    void pub_stat_mixture();
    void pub_stat_nutrient_a();
    void pub_stat_nutrient_b();
    void pub_stat_dose_lockout();
    void pub_stat_dose_lockout_EC();
    void pub_stat_dose_lockout_PH();
    void pub_stat_emergency_stop();
};
# endif