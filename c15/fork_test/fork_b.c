#include <unistd.h>
#include <stdio.h>
int main() {
	printf("I will fork in 5 seconds\n");
	sleep(5);
	int pid = fork();
	if (pid == -1) {
		return -1;
	}

	printf("who am I ? my pid is %d\n", getpid());
	sleep(5);
	return 0;
}
