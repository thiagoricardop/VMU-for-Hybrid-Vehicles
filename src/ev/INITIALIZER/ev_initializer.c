#include "ev_initializer.h"

EvStatusType ev_initializer(void) {

    EvStatusType status = 0;               /* status de retorno (único ponto de saída) (@turn0search0) */
    int32_t shm_fd = -1;
    uintptr_t shm_addr = 0U;            /* evitar Rule 11.5 (@turn1search1) */
    sem_t *tmp_sem = (sem_t *)0;
    mqd_t  tmp_mq = (mqd_t)-1;

    /* 1) Abrir memória compartilhada */
    shm_fd = shm_open(SHARED_MEM_NAME, EV_SHM_FLAGS, EV_SHM_PERMS);
    if (shm_fd < 0) {
        perror("[EV] shm_open falhou");
        status = EXIT_FAILURE;
        goto cleanup;
    }

    /* 2) Mapear em void*, depois converter via uintptr_t para obedecer Rule 11.5 (@turn1search1) */
    shm_addr = (uintptr_t)mmap(NULL, EV_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if ((void *)shm_addr == MAP_FAILED) {
        perror("[EV] mmap falhou");
        status = EXIT_FAILURE;
        goto cleanup;
    }
    system_state = (SystemState *)shm_addr;

    (void)close(shm_fd);

    /* 3) Abrir semáforo POSIX (@turn0search1) */
    tmp_sem = sem_open(SEMAPHORE_NAME, EV_SEM_FLAGS);
    if (tmp_sem == SEM_FAILED) {
        perror("[EV] sem_open falhou");
        status = EXIT_FAILURE;
        goto cleanup;
    }
    sem = tmp_sem;

    /* 4) Configurar atributos da fila de mensagens */
    {
        struct mq_attr ev_attr = {
            .mq_flags   = 0,
            .mq_maxmsg  = EV_MQ_MAXMSG,
            .mq_msgsize = EV_MQ_MSGSIZE,
            .mq_curmsgs = 0
        };

        /* 5) Abrir fila de mensagens (@turn0search1) */
        tmp_mq = mq_open(EV_COMMAND_QUEUE_NAME, EV_MQ_FLAGS, EV_MQ_PERMS, &ev_attr);
        if (tmp_mq == (mqd_t)-1) {
            perror("[EV] mq_open falhou");
            status = EXIT_FAILURE;
            goto cleanup;
        }
        ev_mq = tmp_mq;
    }

cleanup:
    /* 6) Um único ponto de saída retorna status (@turn0search2) */
    return status;
}