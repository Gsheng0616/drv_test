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
