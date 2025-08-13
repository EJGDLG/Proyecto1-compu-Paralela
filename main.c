#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
#include <time.h>

// Prototipos 
float precioAleatorio();
void atenderMesa(int idMesa, float precio);
void cocinarPedidos(int numMesas);
void cobrarPedidos(int numMesas);

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

int main() {
    srand(time(NULL)); // Semilla para aleatorios

    int numMesas = 20;               // Número total de mesas
    float pedidos[20];               // Precio de cada pedido
    double totalVentas = 0.0;        // Total recaudado
    int tiempoInicial = 12;          // Hora inicial de apertura del restaurante

    // Asignar pedidos aleatorios a cada mesa
    int i;
    for (i = 0; i < numMesas; i++) {
        pedidos[i] = precioAleatorio();
    }

    #pragma omp parallel for shared(pedidos) reduction(+:totalVentas) firstprivate(tiempoInicial)
    for (i = 0; i < numMesas; i++) {
        atenderMesa(i, pedidos[i]);
        totalVentas += pedidos[i];
    }

    // Cocinero y cajero trabajando en paralelo
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
    }

    printf("\nTotal recaudado: $%.2lf\n", totalVentas);
    return 0;
}
