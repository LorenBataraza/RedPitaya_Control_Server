#pragma once

// ============================================================================
//  ShmQueue<T>  -  Cola en memoria compartida (POSIX shm + semáforos)
//
//  Versión template del diseño original. Cada cola es una región de memoria
//  compartida con nombre (shm_open) que contiene:
//      - cabecera (punteros putter/getter, qSize, 3 semáforos)
//      - un ring buffer de `qSize` elementos de tipo T (flexible array)
//
//  IMPORTANTE: T debe ser trivialmente copiable (vive en shm, se copia byte a
//  byte en un buffer de tamaño fijo). cmd_t y los structs `packed` lo son;
//  NO se puede usar std::string / punteros a heap dentro de T.
//
//  Los semáforos se inicializan con pshared=1, así la cola sirve tanto entre
//  hilos del mismo proceso como entre procesos distintos.
// ============================================================================

#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstddef>
#include <type_traits>

template <class T>
class ShmQueue {
    static_assert(std::is_trivially_copyable<T>::value,
                  "T debe ser trivialmente copiable (vive en memoria compartida)");

private:
    size_t qSize;   // Cantidad de slots del ring buffer.
    size_t putter;  // Índice de escritura (monótono, se usa % qSize).
    size_t getter;  // Índice de lectura  (monótono, se usa % qSize).

    sem_t mtx;    // Mutex: exclusión mutua sobre los datos.
    sem_t avail;  // Cantidad de elementos disponibles para Get().
    sem_t freeS;  // Cantidad de slots libres para Put().

    T queue[0];   // Flexible array: el buffer real va a continuación (GNU).

public:
    // Crea (o re-crea) una región de memoria compartida con la cola.
    static ShmQueue *Create(const char *qname, size_t qsize) {
        assert(qsize);
        assert(qname[0]);

        // Si quedó una instancia vieja (otra corrida / otro tamaño) la limpio.
        shm_unlink(qname);

        int shmfd = shm_open(qname, O_RDWR | O_CREAT, 0640);
        assert(shmfd != -1);

        size_t total = sizeof(ShmQueue) + qsize * sizeof(T);
        int ret = ftruncate(shmfd, total);
        assert(ret != -1);

        ShmQueue *q = (ShmQueue *)mmap(NULL, total, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, shmfd, 0);
        assert(q != MAP_FAILED);
        close(shmfd);

        q->qSize = qsize;
        q->putter = 0;
        q->getter = 0;
        sem_init(&q->mtx, 1, 1);
        sem_init(&q->avail, 1, 0);
        sem_init(&q->freeS, 1, qsize);
        return q;
    }

    // Se "attacha" a una cola existente con el nombre dado.
    static ShmQueue *Attach(const char *qname) {
        int shmfd = shm_open(qname, O_RDWR, 0640);
        assert(shmfd != -1);

        // Paso 1: mapear sólo la cabecera para leer qSize.
        ShmQueue *tmp = (ShmQueue *)mmap(NULL, sizeof(ShmQueue),
                                         PROT_READ | PROT_WRITE,
                                         MAP_SHARED, shmfd, 0);
        size_t qsize = tmp->qSize;
        munmap(tmp, sizeof(ShmQueue));

        // Paso 2: mapear todo con el tamaño correcto.
        size_t total = sizeof(ShmQueue) + qsize * sizeof(T);
        ShmQueue *q = (ShmQueue *)mmap(NULL, total, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, shmfd, 0);
        close(shmfd);
        return q;
    }

    // Se "detacha" (unmap) de la cola.
    void Detach() {
        munmap(this, sizeof(ShmQueue) + this->qSize * sizeof(T));
    }

    // Destruye semáforos y elimina la región de memoria compartida.
    static void Destroy(const char *qname) {
        ShmQueue *q = Attach(qname);
        sem_destroy(&q->mtx);
        sem_destroy(&q->avail);
        sem_destroy(&q->freeS);
        shm_unlink(qname);
        q->Detach();
    }

    // Inserta un elemento; bloquea si la cola está llena.
    void Put(T elem) {
        sem_wait(&freeS);  // Bloquea si no hay slots libres.
        sem_wait(&mtx);
        queue[putter % qSize] = elem;
        putter++;
        sem_post(&mtx);
        sem_post(&avail);  // Hay un elemento más.
    }

    // Remueve y retorna un elemento; bloquea si la cola está vacía.
    T Get() {
        sem_wait(&avail);  // Bloquea si no hay elementos.
        sem_wait(&mtx);
        T valor = queue[getter % qSize];
        getter++;
        sem_post(&mtx);
        sem_post(&freeS);  // Se liberó un slot.
        return valor;
    }

    // Cantidad de elementos actualmente en la cola.
    size_t Cnt() {
        sem_wait(&mtx);
        size_t cnt = putter - getter;
        sem_post(&mtx);
        return cnt;
    }
};
