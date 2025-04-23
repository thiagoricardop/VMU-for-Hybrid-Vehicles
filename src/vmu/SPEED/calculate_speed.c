#include "calculate_speed.h"

int calculateCicleEstimated( SystemState *state ) {
    double localSpeed = 0;
    int cicles = 0;

    while(localSpeed <= state->speed) {
        
        localSpeed = cicles * (1.13 - (0.00162 * cicles));
        if ( cicles == 350 ) {
            break;
        }

        cicles++;

    }

    return cicles;
}

// Function to calculate the current speed based on engine RPM and transition factor
void calculate_speed(SystemState *state) {
    double fator_variacao = 0.0;
    sem_wait(sem); // Acquire the semaphore
    
    if (state->accelerator) {
        ciclesQuantity = calculateCicleEstimated(state);
        // Calculate speed increase based on RPM of both engines and the transition factor
        state->speed = ciclesQuantity*(1.13 - (0.00162 * ciclesQuantity));
        

    } 
    
    else if (!state->brake) {
        // Coasting - apply a slight deceleration
        state->speed = fmax(state->speed - 0.3, MIN_SPEED); // Ensure speed does not go below the minimum
    } 
    
    else {
        // Braking - apply a stronger deceleration
        double fator_variacao_freio = 2.0;
        state->speed = fmax(state->speed - fator_variacao_freio, MIN_SPEED);
    }

    sem_post(sem); // Release the semaphore
    
}