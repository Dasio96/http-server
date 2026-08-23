{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    gcc
    gnumake
    openssl
    pkg-config
  ];

  shellHook = ''
    export CPATH="${pkgs.openssl.dev}/include:$CPATH"
    export LIBRARY_PATH="${pkgs.openssl.out}/lib:$LIBRARY_PATH"
  '';
}
