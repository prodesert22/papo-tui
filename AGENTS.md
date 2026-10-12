# Instruções para agentes

Este arquivo define como trabalhar neste repositório. Leia-o antes de alterar arquivos.

## Projeto

Papo TUI é um cliente de conversas para terminal, planejado em C. O contrato do serviço está em `openapi.yml`; a arquitetura e as decisões técnicas estão em `docs/decisoes.md`, e o comportamento implementado em `docs/contrato.md`.

O repositório possui uma primeira entrega de login em C17, com CMake, ncursesw/formw, libcurl e Jansson. Há testes HTTP com backend simulado e de interação em pseudoterminal. Não tratar a tela inicial como conclusão de toda a integração. Consulte `docs/decisoes.md` para as bibliotecas e `docs/contrato.md` para o fluxo implementado.

## Regras de trabalho

1. Leia `README.md`, `docs/decisoes.md`, `docs/contrato.md` e os trechos relevantes de `openapi.yml` antes de propor ou implementar uma entrega.
2. Mantenha cada mudança limitada à entrega em andamento. Não misture funcionalidades futuras ou refatorações sem relação com a tarefa.
3. Não invente endpoints, campos, eventos, permissões ou garantias de sincronização. Quando a especificação não definir um comportamento, registre a lacuna e escolha uma solução que não prometa suporte inexistente no backend.
4. Trate cada instância como um contexto isolado. Cookies, estado de sessão e entidades do mesmo UUID em instâncias diferentes não podem ser compartilhados.
5. Nunca grave senhas ou cookies em logs, arquivos de configuração versionados ou mensagens de erro. Redija segredos de saídas de diagnóstico.
6. Considere todo conteúdo remoto como texto não confiável. Não permita que mensagens, nomes ou tópicos emitam sequências de controle do terminal.
7. Em C, defina claramente quem possui e libera cada buffer e objeto. Verifique limites, falhas de alocação, cancelamento e encerramento de conexões.
8. Mantenha `README.md` e `docs/` coerentes com o comportamento realmente implementado. Atualize documentação quando uma mudança alterar o uso ou uma decisão do projeto. Alterações no contrato OpenAPI exigem confirmação do comportamento do backend.
9. Prefira alterações pequenas e revisáveis. Não declare uma entrega concluída sem cumprir os critérios de aceite e registrar evidências. Registre novas funcionalidades e suas dependências antes de alterar o escopo do MVP.
10. Use bibliotecas existentes para a funcionalidade que será adicionada. Antes de escrever uma implementação própria, verifique se as dependências do projeto já oferecem o recurso; caso não ofereçam, pesquise bibliotecas compatíveis com C e com a stack do projeto. Avalie manutenção, licença, documentação e custo de integração. Implemente o recurso do zero somente quando não houver biblioteca adequada e registre a justificativa técnica.
11. Escreva todas as mensagens de commit em inglês, incluindo o título e o corpo quando houver.

## Build e validação

O build utiliza CMake e pkg-config. Mantenha os pré-requisitos e comandos no README alinhados ao código. Não afirme que uma validação foi executada sem evidência. Os comandos são:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Execute os testes apropriados às mudanças antes de afirmar que estão validadas. Para mudanças na integração com o backend, inclua cenários de falha e valide contra a instância de desenvolvimento quando disponível. Não coloque credenciais reais em fixtures.

## Organização pretendida

`src/ui/` contém o formulário de login e `src/net/` o transporte e o estado de autenticação. `tests/` contém testes e backend simulado; `docs/` documenta o contrato e as decisões. À medida que a implementação crescer, manter responsabilidades separadas para ciclo e estado do aplicativo, interface, transporte/API, autenticação, modelos e configuração. Só criar módulos e diretórios quando houver código que os utilize.
