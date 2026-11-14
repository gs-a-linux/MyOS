#include <iostream>
#include <sys/reboot.h>
#include <unistd.h>
using namespace std;

int main(){
    reboot(RB_POWER_OFF);
}
