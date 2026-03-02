#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(){
    int semaforoRojo = 0;
    int semaforoAmarillo = 0;
    int semaforoVerde = 0;

    //Siempre inicia en rojo
    while(1){
        printf("Semaforo Rojo: %d\n", semaforoRojo);
        printf("Semaforo Amarillo: %d\n", semaforoAmarillo);
        printf("Semaforo Verde: %d\n", semaforoVerde);
        printf("\n");

        if(semaforoRojo == 1){
            semaforoRojo = 0;
            semaforoAmarillo = 1;
            printf("[AMARILLO]\n");
            cuenta_regresiva(5);
            
        }
        else if(semaforoAmarillo == 1){
            semaforoAmarillo = 0;
            semaforoVerde = 1;
        }
        else if(semaforoVerde == 1){
            semaforoVerde = 0;
            semaforoRojo = 1;
        }
    }
}


void cuenta_regresiva(int segundos) {
    while (segundos > 0) {
        printf("Tiempo restante: %d segundos\r", segundos);
        fflush(stdout);  // fuerza a imprimir en tiempo real
        sleep(1);
        segundos--;
    }
}