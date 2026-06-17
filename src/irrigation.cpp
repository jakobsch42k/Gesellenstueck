#include "irrigation.h"
#include "actuators.h"
#include "logger.h"

void IrrigationController::enterState(IrrigationState next) {
    state          = next;
    stateEnteredAt = millis();
}

void IrrigationController::init(Actuators& actuators) {
    act = &actuators;
    act->valve_closeAll();
    act->pump_off();
    enterState(IRRIGATION_IDLE);
    logger.info("irrigation", "init OK");
}

void IrrigationController::update(const LiveData& data, Config& cfg, ErrorFlags& err) {
    // Hard stop: critical water level or emergency stop locks irrigation
    if (err.ERR_WATER_CRITICAL || err.EMERGENCY_STOP) {
        if (state != IRRIGATION_IDLE) {
            act->pump_off();
            act->valve_closeAll();
            enterState(IRRIGATION_IDLE);
            activeBed = -1;
            logger.warn("irrigation", "locked — water critical or emergency stop");
        }
        return;
    }

    // Low water warning — log but continue
    if (!data.waterLow && state == IRRIGATION_IDLE) {
        err.lastErrorMessage = "Water level low — refill soon";
    }

    unsigned long elapsed = millis() - stateEnteredAt;

    switch (state) {

        case IRRIGATION_IDLE: {
            // Round-robin scan: start from cfg.nextBed and wrap, watering the
            // first dry bed found. Rotating the start point keeps beds fair under
            // contention so no bed starves behind a perpetually thirsty bed 1.
            // Bounded to NUM_BEDS iterations — safe even if all beds are
            // faulty/moist (loop simply finds nothing and stays IDLE).
            for (int k = 0; k < NUM_BEDS; k++) {
                int i = (cfg.nextBed + k) % NUM_BEDS;
                if (err.ERR_SOIL[i]) continue; // skip faulty sensors
                if (data.soilPerc[i] < cfg.moisture[i]) {
                    activeBed = i;
                    act->valve_open(activeBed);
                    act->pump_on(cfg.pumpPwm);
                    enterState(IRRIGATION_PUMPING);
                    logger.info("irrigation", "bed " + String(i + 1) +
                                " dry (" + String(data.soilPerc[i]) + "%) — pumping");
                    break;
                }
            }
            break;
        }

        case IRRIGATION_PUMPING:
            if (elapsed >= cfg.irrigationDuration_ms) {
                act->valve_close(activeBed);
                act->pump_off();
                enterState(IRRIGATION_PAUSING);
                logger.info("irrigation", "bed " + String(activeBed + 1) +
                            " — pump done, diffusion pause");
            }
            break;

        case IRRIGATION_PAUSING:
            if (elapsed >= cfg.irrigationPause_ms) {
                // Cycle fully done with an actual watering — advance the rotation
                // pointer past the bed just served so the next bed gets first
                // dibs. Wraps after the last bed (bed 5 → bed 1). Only reached on
                // real watering; the critical-water lock path bypasses this.
                cfg.nextBed = (activeBed + 1) % NUM_BEDS;
                activeBed = -1;
                enterState(IRRIGATION_IDLE);
                // IDLE will re-check moisture on next call
                logger.info("irrigation", "pause done — re-checking moisture");
            }
            break;
    }
}
