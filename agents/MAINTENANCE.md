# Is the code ready for a PR?

## Things to check

- Ensure all rules and steps have been followed.
- Check all changes and optimise for maintainability and readability.
- Always use Australian English in both code and documentation. DO NOT use American English variants.
- Avoid leaving comments that do not contribute to understanding the code. Docstrings, especially, should be omitted if the function can simply be renamed to something more descriptive.
- Docstrings should NOT contain Args/Returns because this is usually made redundant by the function's typing. If the function's arguments and returns are not clear, this means the function should be refactored.
- Check for redundant tests and code and clean them up. Make sure all tests are passing.