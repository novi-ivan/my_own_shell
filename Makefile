CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -pthread
LDFLAGS = -static -pthread

SRC = src/main.cpp
TARGET = kubsh

PKG_DIR = kubsh-package
PKG_BIN = $(PKG_DIR)/usr/bin
PKG_DEBIAN = $(PKG_DIR)/DEBIAN
DEB_NAME = kubsh_1.0_amd64.deb

.PHONY: build run deb clean

# 1) Компиляция из исходников
build: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

# 2) Запуск kubsh
run: $(TARGET)
	./$(TARGET)

# 3) Сборка deb-пакета
deb: build
	@echo "==> Preparing package directory"
	mkdir -p $(PKG_BIN)
	mkdir -p $(PKG_DEBIAN)

	@echo "==> Creating control file"
	@echo "Package: kubsh" > $(PKG_DEBIAN)/control
	@echo "Version: 1.0" >> $(PKG_DEBIAN)/control
	@echo "Section: shells" >> $(PKG_DEBIAN)/control
	@echo "Priority: optional" >> $(PKG_DEBIAN)/control
	@echo "Architecture: amd64" >> $(PKG_DEBIAN)/control
	@echo "Maintainer: Student <student@example.com>" >> $(PKG_DEBIAN)/control
	@echo "Description: Custom shell with VFS support" >> $(PKG_DEBIAN)/control
	@echo " A custom shell implementation with virtual users filesystem." >> $(PKG_DEBIAN)/control

	cp $(TARGET) $(PKG_BIN)/
	chmod 755 $(PKG_BIN)/$(TARGET)

	@echo "==> Building .deb package"
	dpkg-deb --root-owner-group --build $(PKG_DIR) $(DEB_NAME)
	cp $(DEB_NAME) kubsh.deb

	@echo "==> Done: $(DEB_NAME) (and kubsh.deb copy)"

# 4) Очистка проекта
clean:
	rm -f $(TARGET)
	rm -f kubsh_*.deb kubsh.deb
	rm -rf $(PKG_DIR)