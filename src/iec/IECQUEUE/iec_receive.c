#include "iec_receive.h"

EngineCommandIEC iec_receive (EngineCommandIEC cmd) {
    // Receive commands from the VMU through the message queue
    while (mq_receive(iec_mq, (char *)&cmd, sizeof(cmd), NULL) == -1) {

    }

    return cmd;
}