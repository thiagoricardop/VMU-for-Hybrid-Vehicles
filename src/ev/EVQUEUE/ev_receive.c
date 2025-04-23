#include "ev_receive.h"

EngineCommandEV ev_receive (EngineCommandEV cmd) {
    // Receive commands from the VMU through the message queue
    while (mq_receive(ev_mq, (char *)&cmd, sizeof(cmd), NULL) == -1) {

    }

    return cmd;
}