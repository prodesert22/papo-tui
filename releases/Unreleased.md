# Unreleased

- Use ncurses 6.6 consistently in Linux builds so rapid mouse drags preserve
  text selection and copying, matching the development and Windows builds.

- Explain server-reported IP blocks during private instance authorization instead
  of incorrectly attributing every HTTP 403 to the account. Other access refusals
  identify instance authorization, account login or session validation.
