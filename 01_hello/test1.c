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
//fsfsfwrwrwe
//fsdsfs
//fs
//fs
//f
//s
//fs
//
//f
//sf
//s
//
//

// 冒泡排序函数
void bubbleSort(int arr[], int n) {
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                // 交换元素
                int temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

// 打印数组函数
void printArray(int arr[], int n) {
    for (int i = 0; i < n; i++) {
        printf("%d ", arr[i]);
    }
    printf("\n");
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
