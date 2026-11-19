# REIS PSX

Launcher de jogos de PS1 para PS2, no estilo do OPL. Lista os jogos em `.VCD`,
mostra um menu e inicia o jogo escolhido pelo **POPStarter**.

> Este projeto contem apenas o launcher. `POPSTARTER.ELF`, `POPS.ELF` e
> `IOPRP252.IMG` NAO acompanham o REIS PSX: o usuario precisa colocá-los em
> `mass:/POPS/` por conta propria.

## Estado atual (etapa 1)

- [x] Scanner de `.VCD` em `mass:/POPS/`
- [x] Extracao do ID do jogo (`SLUS_007.94`) e nome de exibicao
- [x] Menu com navegacao pelo controle
- [x] Lancamento via copia de `POPSTARTER.ELF` (`XX.<nome>.ELF`)
- [ ] Capas (`ART/<ID>_COV.png`)
- [ ] VMC (cartao de memoria virtual)
- [ ] Favoritos, temas, tempo de jogo
- [ ] Suporte a HDD e SMB

## Como compilar

Requer o [PS2SDK](https://github.com/ps2dev/ps2dev) com o gsKit instalado.

```sh
export PS2DEV=/usr/local/ps2dev
export PS2SDK=$PS2DEV/ps2sdk
export GSKIT=$PS2DEV/gsKit
export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin
make
```

Gera `REISPSX.ELF`.

## Como usar

Estrutura no pendrive (FAT32):

```
POPS/
  POPSTARTER.ELF
  POPS.ELF
  IOPRP252.IMG
  SLUS_007.94.Nome do Jogo.VCD
```

1. Abra o `REISPSX.ELF` pelo uLaunchELF ou FreeMcBoot (com o USB ja carregado).
2. Use cima/baixo para escolher, **X** para jogar.
3. **SELECT** atualiza a lista, **L1/R1** pulam de pagina, **START** sai.

## Notas

- O REIS PSX nao reseta o IOP, para manter o driver USB que o launcher anterior carregou.
- Esta versao foi escrita sem testar em console. O trecho de lancamento (nome do ELF e
  `LoadExecPS2`) é a parte que mais precisa de teste no hardware.
