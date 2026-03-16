#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//MARCOS AGUSTIN GARCIA MOMBELLO

int ordenarDescendente(int[], int);
int ordenarAscendente(int[], int);


int main(){

    int arrayNumeros[10];
    int opcion;

    printf("Ingrese 10 numeros:\n");
    for(int i = 0; i < 10; i++){
        scanf("%d", &arrayNumeros[i]);
    }
    
    do{
        printf("Elija una opción:\n");
        printf("1. ORDENAR ASCENDENTE\n");
        printf("2. ORDENAR DESCENDENTE\n");
        printf("0. SALIR\n");
        scanf("%d", &opcion);
        switch (opcion)
        {     
            case 0:
                printf("Saliendo del programa...\n");
                break;     
            case 1:
                ordenarAscendente(arrayNumeros, 10);
                printf("Numeros ordenados de forma ascendente:\n");
                for(int i = 0; i < 10; i++){
                    printf("%d ", arrayNumeros[i]);
                }
                printf("\n");
                break;
            case 2:
                ordenarDescendente(arrayNumeros, 10);
                printf("Numeros ordenados de forma descendente:\n");
                for(int i = 0; i < 10; i++){
                    printf("%d ", arrayNumeros[i]);
                }
                printf("\n");
                break;
            default:
                printf("Opción inválida.\n");
                break;
        }
    }
    while(opcion != 0);
    return 0;
}

int ordenarAscendente(int arrayNumeros[], int size){
    for(int i = 0; i < size - 1; i++){
        for(int j = 0; j < size - i - 1; j++){
            if(arrayNumeros[j] > arrayNumeros[j + 1]){
                int temp = arrayNumeros[j];
                arrayNumeros[j] = arrayNumeros[j + 1];
                arrayNumeros[j + 1] = temp;
            }
        }
    }
}

int ordenarDescendente(int arrayNumeros[], int size){
    for(int i = 0; i < size - 1; i++){
        for(int j = 0; j < size - i - 1; j++){
            if(arrayNumeros[j] < arrayNumeros[j + 1]){
                int temp = arrayNumeros[j];
                arrayNumeros[j] = arrayNumeros[j + 1];
                arrayNumeros[j + 1] = temp;
            }
        }
    }
}