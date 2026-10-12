# Decisões técnicas

## Interface inicial: ncursesw + formw

A tela de login utiliza ncursesw e formw, já disponíveis no ambiente. formw oferece edição, navegação, campos dinâmicos e entrada privada pela opção `O_PUBLIC`, evitando implementar um editor de campos próprio.

Notcurses Core era a primeira escolha do planejamento, mas não está instalado neste ambiente. Seu widget ncreader não documenta opção de senha oculta. Para esta entrega, a biblioteca de formulários existente atende diretamente aos quatro campos pedidos. A escolha atual passa a ser ncursesw/formw; mudanças futuras de renderizador exigem justificativa e revisão do plano.

O tema adapta-se a 256 cores ou à paleta básica e preserva texto em resize. formw preenche buffers com espaços: o adaptador acompanha a extensão digitada para preservar espaços finais de senhas sem confundi-los com padding. Lê a posição absoluta no membro `curcol` da estrutura pública ncurses FORM; a edição permanece na biblioteca. Builds com `NCURSES_OPAQUE_FORM` não são suportados pelo adaptador inicial.

`O_BS_OVERLOAD` e `O_NL_OVERLOAD` ficam desativados: a navegação é explícita, e Backspace no início do input não troca de campo. O cursor fica oculto durante a pintura e reaparece na posição do campo focado após a atualização; a tela só é redesenhada quando ocorre entrada ou mudança de estado.

Mouse utiliza `mousemask`, `getmouse` e `wmouse_trafo` do ncurses. Apenas botão esquerdo ativa controles; eventos de soltura não repetem ações. Campos podem ser focados e o cursor posicionado pelo clique, preservando o deslocamento de rolagem horizontal. Login e logout compartilham o mesmo fluxo de ação para mouse e Enter.

Os testes utilizam [pyte](https://pyte.readthedocs.io/en/latest/api.html) para reconstruir a tela e observar o cursor, além de emitir eventos de mouse via pseudoterminal. A biblioteca é dependência de desenvolvimento, sem participação no executável. Fontes do ncurses: [opções do formulário](https://invisible-island.net/ncurses/man/form_opts.3x.html) e [mouse](https://invisible-island.net/ncurses/man/curs_mouse.3x.html).

Fontes oficiais: [ncurses](https://invisible-island.net/ncurses/), [opções de campos](https://invisible-island.net/ncurses/man/form_field_opts.3x.html), [driver de formulários](https://invisible-island.net/ncurses/man/form_driver.3x.html) e [ncreader](https://notcurses.com/notcurses_reader.3.html).

## Transporte e serialização

libcurl fornece parsing de URLs, verificação TLS, cookie engine e HTTP via multi. Uma tentativa possui um request ativo; o ciclo da UI verifica progresso a cada 30 ms sem esperar a transferência terminar. Jansson produz e valida JSON. Redirecionamentos não são seguidos; uma nova tentativa limpa a sessão anterior.

Limites iniciais do cliente: URL até 2048 bytes, usuário até 256 células, senhas até 1024 células, resposta HTTP até 1 MiB, timeout de conexão de 5 segundos e de transferência de 15 segundos. São proteções do cliente, não limites confirmados do backend. WebSocket permanece nas próximas entregas.

Fontes oficiais: [libcurl multi](https://curl.se/libcurl/c/curl_multi_perform.html), [URLs](https://curl.se/libcurl/c/libcurl-url.html) e [Jansson](https://github.com/akheron/jansson).

## Ambiente da primeira entrega

| Componente | Versão encontrada | Licença da dependência |
| --- | --- | --- |
| GCC | 16.2.1 | GPL com exceção para runtime |
| ncursesw / formw | 6.6.20251230, conforme pkg-config | MIT/X11 |
| libcurl | 8.22.0 | curl license |
| Jansson | 2.15.1 | MIT |
| Python dos testes | 3.12.14 | PSF |

As versões mínimas do CMake são requisitos de API, não uma afirmação de que todas foram exercitadas. Não foi definida uma licença para o código do projeto nesta entrega.
