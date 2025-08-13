#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
#include <time.h>

// __________________________
// PROTOTIPOS DE FUNCIONES
// __________________________
float precioAleatorio();
void atenderMesa(int idMesa, float precio);
void cocinarPedidos(int numMesas);
void cobrarPedidos(int numMesas);
void recogerPlatos(int numMesas); // Nueva tarea para sections

// Genera un precio aleatorio entre 5 y 25
float precioAleatorio() {
    return (rand() % 21) + 5;
}

// Simula que un mesero atiende una mesa
void atenderMesa(int idMesa, float precio) {
    printf("Mesa %d atendida, pedido de $%.2f\n", idMesa, precio);
}

// Simula que el cocinero prepara pedidos
void cocinarPedidos(int numMesas) {
    printf("Cocinero: preparando %d pedidos...\n", numMesas);
}

// Simula que el cajero cobra pedidos
void cobrarPedidos(int numMesas) {
    printf("Cajero: cobrando %d pedidos...\n", numMesas);
}

// Nueva función: simula que un mesero recoge platos
void recogerPlatos(int numMesas) {
    printf("Mesero: recogiendo platos de %d mesas...\n", numMesas);
}

int main() {
    srand(time(NULL)); // Semilla para números aleatorios

    int numMesas = 20;               // Número total de mesas en el restaurante
    float pedidos[20];               // Precio de cada pedido (array compartido)
    double totalVentas = 0.0;        // Total recaudado (variable compartida y usada con reduction)
    int tiempoInicial = 12;          // Hora inicial de apertura del restaurante (firstprivate)

    // ASIGNAR PEDIDOS ALEATORIOS
    int i;
    for (i = 0; i < numMesas; i++) {
        pedidos[i] = precioAleatorio();
    }

    // ATENDER MESAS EN PARALELO
    // __________________________
    // - shared(pedidos): todos los hilos comparten el arreglo de precios
    // - firstprivate(tiempoInicial): cada hilo tiene su propia copia de la hora inicial
    // - reduction(+:totalVentas): cada hilo suma a totalVentas de forma segura
    #pragma omp parallel for shared(pedidos) reduction(+:totalVentas) firstprivate(tiempoInicial)
    for (i = 0; i < numMesas; i++) {
        atenderMesa(i, pedidos[i]);
        totalVentas += pedidos[i];
    }

    // TAREAS DIFERENTES EN PARALELO
    // _____________________________
    // Usamos parallel sections para ejecutar tareas distintas al mismo tiempo:
    // - Cocinar pedidos
    // - Cobrar pedidos
    // - Recoger platos (tarea añadida)
    #pragma omp parallel sections
    {
        #pragma omp section
        {
            cocinarPedidos(numMesas);
        }
        #pragma omp section
        {
            cobrarPedidos(numMesas);
        }
        #pragma omp section
        {
            recogerPlatos(numMesas);
        }
    }

    // __________________________
    // RESULTADO FINAL
    // __________________________
    printf("\nTotal recaudado: $%.2lf\n", totalVentas);
    return 0;
}
