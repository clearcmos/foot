pkgname=foot-custom
pkgdesc='A fast, lightweight and minimalistic Wayland terminal emulator (fork with tabs)'
pkgver=1.26.1.r6906.g5af8bf7c
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
             tllist
             wayland-protocols)
provides=('foot')
conflicts=('foot')
source=("$pkgname::git+https://github.com/clearcmos/foot.git")
sha256sums=('SKIP')

pkgver() {
    cd "$pkgname"
    # Monotonic: total commit count always increases, so every rebuild reads
    # as an upgrade. The tag 1.26.1 is not in this fork, so a tag..HEAD count
    # would always be 0 and pacman would compare the commit hash instead
    # (non-monotonic, and the source of spurious "downgrading" warnings).
    printf "1.26.1.r%s.g%s" \
        "$(git rev-list --count HEAD)" "$(git rev-parse --short=8 HEAD)"
}

build() {
    arch-meson "$pkgname" build --wrap-mode default
    meson compile -C build
}

package() {
    meson install -C build --destdir "$pkgdir"
    # Remove terminfo files that conflict with ncurses
    rm -rf "$pkgdir/usr/share/terminfo"
    install -Dm0644 -t "$pkgdir/usr/share/licenses/$pkgname/" "$pkgname/LICENSE"
}
