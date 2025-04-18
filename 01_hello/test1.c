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
int binarySearch(int arr[], int size, int target)
{
    int left = 0;
    int right = size - 1;
    while (left <= right)
    {
        int mid = left + (right - left) / 2;
        if (arr[mid] == target)
        {
            return mid;
        }   
        else if (arr[mid] < target) 
        {
            left = mid + 1;
        }
        else
        {
            right = mid - 1;    
                        
        }
    }
}