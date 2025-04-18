#include <stdio.h>
//daddadasd
int main(int argc, char * argv [ ])
{	
	int *p = NULL;

    printf("address [0x%p]\r\n", p);
	*p = 0;
	
    return 0;
}
void func()    
{
    int *p = NULL;
    printf("address [0x%p]\r\n", p);    
    *p = 0;
    printf("address [0x%p]\r\n", p);    
    
}

int main() {
    int arr[] = {64, 34, 25, 12, 22, 11, 90};
    int n = sizeof(arr) / sizeof(arr[0]);
    
    printf("原始数组: ");
    printArray(arr, n);
    
    bubbleSort(arr, n);
    
    printf("排序后的数组: ");
    printArray(arr, n);
    
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
    return -1;
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

int quick_sort(int s[], int l, int r)   
{
    if (l < r)
    {
        int i = l, j = r, x = s[l];
        while (i < j)
        {
            while(i < j && s[j] >= x)
                j--;
            if(i < j)
                s[i++] = s[j];
            while(i < j && s[i] < x)
                i++;
            if(i < j)
                s[j--] = s[i];
        }
        s[i] = x;
        quick_sort(s, l, i - 1);
        quick_sort(s, i + 1, r);

    }
    
}