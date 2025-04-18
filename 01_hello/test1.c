#include <stdio.h>
//daddadasd
int main(int argc, char * argv [ ])
{	
	int *p = NULL;

    printf("address [0x%p]\r\n", p);
	*p = 0;
	
    return 0;
}

int main()  
{
    int a = 10;
    int *p = &a;
    printf("address [0x%p]\r\n", p);
    *p = 20;
    printf("a = %d\r\n", a);
    return 0;
}

int bubble_sort(int *a, int len)
{
    int i, j, temp;
    for (i = 0; i < len - 1; i++)
        for (j = 0; j < len - 1 - i; j++)
            if (a[j] > a[j+1])
            {
                temp = a[j];
                a[j] = a[j+1];
                a[j+1] = temp;
                        
            }
}