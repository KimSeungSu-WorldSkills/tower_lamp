# Tower lamp C API

Linux 시리얼 장치용 C 라이브러리. 외부 라이브러리 없이 `tower_lamp.c`를 애플리케이션과 함께 컴파일한다.

```sh
cc -std=c11 -Wall -Wextra -Werror -c tower_lamp.c
python3 test_tower_lamp.py
```

## 색상과 부저의 관계

첨부된 명령표의 패킷은 `FF 색상 부저 점멸 AA`인 바이너리 5바이트다. 이전 테스트의 `FF 02 02 01 AA`는 **녹색 + 부저 켜기**를 함께 지정했으므로 녹색이 켜졌다. 부저만 울리고 램프를 끄려면 `FF 01 02 01 AA`다.

각 필드는 항상 함께 전송해야 하므로, C API는 마지막으로 응답 확인된 설정을 저장하고 개별 setter가 나머지 필드를 유지한다. 점멸 필드는 LED와 부저가 공유한다. 제공된 표에 주기 설명이 모호한 부분이 있어 `BLINK_2`~`BLINK_4`는 원래 코드값으로만 명명했다.

## 사용 예

```c
#include "tower_lamp.h"
#include <stdio.h>

int main(void)
{
    tower_lamp_t *lamp = tower_lamp_open(
        "/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0", 9600, 1500);
    if (!lamp) { perror("open"); return 1; }

    /* 처음에는 원하는 전체 상태를 명시한다: 램프/부저 모두 끔. */
    if (tower_lamp_off(lamp) < 0) goto error;
    /* 색상은 OFF를 유지하고 부저만 켠다. */
    if (tower_lamp_set_buzzer(lamp, true) < 0) goto error;
    /* 필요한 시간 동안 애플리케이션에서 대기한 뒤 호출한다. */
    if (tower_lamp_set_buzzer(lamp, false) < 0) goto error;

    tower_lamp_close(lamp);
    return 0;
error:
    perror("tower_lamp");
    /* 응답 유실이어도 장치가 켜졌을 수 있으므로 명시적으로 끄기를 시도. */
    if (tower_lamp_off(lamp) < 0) perror("off");
    tower_lamp_close(lamp);
    return 1;
}
```

`tower_lamp_apply()`는 전체 상태를 한 번에 설정한다. `set_color()`, `set_buzzer()`, `set_blink()`는 해당 필드만 변경한다. `get_state()`는 장치 조회가 아닌 마지막 확인 상태를 반환한다.

장치 상태 조회 명령은 제공된 표에 없다. 연결 직후 또는 통신 실패 후에는 개별 setter가 `ENODATA`로 실패한다. 이때 `apply()` 또는 `off()`로 전체 상태를 다시 지정한다. 정상 반환은 명령과 일치하는 응답을 받았다는 의미이며, 실제 빛과 소리까지 검증하지는 않는다.

오류는 `-1`과 `errno`로 전달한다(`EINVAL`, `ETIMEDOUT`, `ENODATA`, 운영체제 I/O 오류 등). 포트는 협조적인 프로세스 사이에서 `flock`으로 잠근다. 동일 핸들 호출은 애플리케이션에서 직렬화해야 하며, 다른 프로그램이 포트를 직접 제어하면 캐시가 실제 상태와 달라질 수 있다.

`open()`은 출력 상태를 바꾸지 않는다. `close()`도 출력을 끄지 않으므로 필요하면 먼저 `off()`를 호출한다. 기존 Python CLI는 이전 방식 그대로 별도로 남겨 두었다.
