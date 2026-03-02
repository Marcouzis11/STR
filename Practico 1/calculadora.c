#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void suma(double, double);
void resta(double, double);
void multiplicacion(double, double);
void division(double, double);

//MARCOS AGUSTIN GARCIA MOMBELLO

int main(){

    double num1, num2;
    int opcion;


    do{

    printf("Ingrese el primer numero: ");
    scanf("%lf", &num1);
    printf("\n");

    printf("Ingrese el segundo numero: ");
    scanf("%lf", &num2);
    printf("\n");

    printf("Elija una opción:\n");
    printf("1. SUMA\n");
    printf("2. RESTA\n");
    printf("3. MULTIPLICACION\n");
    printf("4. DIVISION\n");
    printf("0. SALIR\n");
    scanf("%d", &opcion);
    
        switch (opcion)
        {
        case 1:
            suma(num1, num2);
            break;
        case 2:
            resta(num1, num2);
            break;
        case 3:
            multiplicacion(num1, num2);
            break;
        case 4:
            division(num1, num2);
            break;
        default:
            printf("Opción inválida.\n");
            break;
        }
    }
    while(opcion != 0);
    return 0;

}

void suma(double num1, double num2){
    double res = num1 + num2;
    printf("SUMA: %lf + %lf = %lf\n", num1, num2, res);
}

void resta(double num1, double num2){
    double res = num1 - num2;
    printf("RESTA: %lf - %lf = %lf\n", num1, num2, res);
}

void multiplicacion(double num1, double num2){
    double res = num1 * num2;
    printf("MULTIPLICACION: %lf * %lf = %lf\n", num1, num2, res);
}

void division(double num1, double num2){
    if(num2 != 0){
        double res = num1 / num2;
        printf("DIVISION: %lf / %lf = %lf\n", num1, num2, res);
    } else {
        printf("Error: No se puede dividir por cero.\n");
    }
}
