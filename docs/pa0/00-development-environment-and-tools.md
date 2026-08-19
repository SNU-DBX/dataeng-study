# 0 · 개발 환경과 기본 도구
 
## Required Tools

C/C++ 프로그램을 작성하고 실행하기 위해서는 코드 작성, 버전 관리, 빌드 및 디버깅을 위한 도구가 필요합니다.

이번 챕터에서는 소스 코드를 작성하기 위한 **Editor**와 변경 이력을 관리하는 **Git**의 기본 사용법을 소개합니다.
- **Editor**: 소스 코드를 작성하고 수정하는 도구입니다. 여기서는 `vi`의 기본 사용법을 다룹니다.
- **Git**: 소스 코드의 변경 이력을 기록하고 관리하는 버전 관리 시스템입니다.

아래 도구들은 이후 [7. 빌드, 디버깅](07-build-and-debugging.md)에서 자세히 다루겠습니다.
- **Compiler**: C/C++ 소스 코드를 기계가 실행할 수 있는 형태로 변환할 수 있습니다.
- **make**: 작성된 규칙에 따라 빌드 명령을 자동화할 수 있습니다.
- **GDB**:  실행 중인 프로그램의 내부 상태를 관찰하고 제어할 수 있습니다.

다음 명령으로 각 도구의 설치 여부와 버전 정보를 확인할 수 있습니다.

```sh
cc --version      # C compiler
c++ --version     # C++ compiler

make --version
git --version

gdb --version     # Linux
lldb --version    # macOS
```

!!! note "Language Standard"
    해당 부트캠프의 C 예제는 **C11**, C++ 예제는 **C++17** 표준을 기준으로 작성되었습니다.
    C++ 예제를 컴파일할 때는 `-std=c++17` 옵션을 사용해야 합니다.

    ```sh
    c++ -std=c++17 source.cpp -o program
    ```

!!! warning "Debugger"
    macOS에서는 GDB 대신 LLDB가 기본으로 설치되어 있습니다.
    GDB와 LLDB의 명령은 일부 다를 수 있으니, 확인이 필요합니다.

## vi

`vi`는 명령줄 환경에서 사용할 수 있는 텍스트 편집기입니다.
기본적으로 일반 모드에서 시작하며, `i`를 눌러 입력 모드로 전환해야 텍스트를 작성할 수 있습니다.

다음은 파일 작성에서 자주 사용하는 기본 명령어입니다.

| Command | Description                    |
| ------- | ------------------------------ |
| `i`     | 입력 모드로 전환합니다.                  |
| `Esc`   | 입력 모드에서 일반 모드로 돌아갑니다.          |
| `:w`    | 파일을 저장합니다.                     |
| `:q`    | 편집기를 종료합니다.                    |
| `:wq`   | 파일을 저장하고 종료합니다.                |
| `:q!`   | 변경 사항을 저장하지 않고 종료합니다.          |
| `/text` | `text`를 검색합니다.                 |
| `dd`    | 현재 줄을 삭제합니다.                   |
| `u`     | 마지막 변경을 취소합니다.                 |
| `yy`    | 현재 줄을 복사합니다.                   |
| `p`     | 복사한 줄을 현재 위치의 바로 아래 줄에 붙여넣습니다. |

파일을 작성하고 저장하는 기본 흐름은 다음과 같습니다.

```text
vi main.c → i → 코드 입력 → Esc → :wq
```

## Git

Git은 파일의 변경 이력을 기록하고 여러 버전의 소스 코드를 관리하는 도구입니다.

원격 저장소를 처음 내려받을 때는 `git clone`을 사용합니다.
```sh
git clone https://github.com/{user}/{repository}.git
cd {repository}
```

저장소에서 파일을 수정한 뒤 다음과 같은 순서로 변경 내용을 확인하고 기록할 수 있습니다.
```sh
git status
git diff

git add main.c
git diff --staged

git commit -m "Add C example"
git log --oneline
```

- `git status`: 변경된 파일과 Git의 현재 상태를 확인합니다.
- `git diff`: 아직 기록하지 않은 변경 내용을 확인합니다.
- `git add`: 커밋에 포함할 파일을 선택합니다.
- `git diff --staged`: 다음 커밋에 포함될 변경 내용을 확인합니다.
- `git commit`: 선택한 변경 사항을 하나의 버전으로 기록합니다.
- `git log`: 커밋 이력을 확인합니다.

커밋하기 전에는 `git status`와 `git diff --staged`를 사용하여 의도한 파일과 내용만 포함되었는지 확인하는 것이 좋습니다.
