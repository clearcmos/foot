pkgname=foot-custom
pkgdesc='A fast, lightweight and minimalistic Wayland terminal emulator (fork with tabs)'
pkgver=1.26.1.r0.eedb08b
pkgrel=1
arch=('x86_64')
url='https://github.com/clearcmos/foot'
license=('MIT')
depends=(fcft
         fontconfig
         libutf8proc
         libxkbcommon
         ncurses
         pixman
         wayland)
makedepends=(git
             meson
             scdoc
             wayland-protocols)
provides=('foot')
conflicts=('foot')
source=("$pkgname::git+https://github.com/clearcmos/foot.git")
sha256sums=('SKIP')

pkgver() {
    cd "$pkgname"
    printf "1.26.1.r%s.%s" "$(git rev-list 1.26.1..HEAD --count 2>/dev/null || echo 0)" "$(git rev-parse --short HEAD)"
}

build() {
    arch-meson "$pkgname" build
    meson compile -C build
}

package() {
    meson install -C build --destdir "$pkgdir"
    install -Dm0644 -t "$pkgdir/usr/share/licenses/$pkgname/" "$pkgname/LICENSE"
}
