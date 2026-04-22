# Development Style
- Follow SOLID programming principles.
- Follow principles of Test-Driven Development (TDD).
  1. Write tests before modifying or adding code any code.
  2. Check the tests fail expectedly.
  3. Modify the code to make the tests pass.
  4. Check the tests pass.
  5. Repeat steps 1-4 each time you intend to make another change.
- Follow DRY (Don't Repeat Yourself) principles.
- Adhere to MVC (Model-View-Controller) architecture when making changes to any code in the `src` directory.
- We practice branch development. Ensure that code is never checked into main
- Branch naming MUST be in the format of 'feat/{short-description}'. This is to ensure that the branch is easily identifiable and can be easily merged into main.
- Primary keys should always be UUIDs.

# General Steps 
1. Read the Acceptance Criteria from the Linear issue if there is one provided.
   1. If anything is unclear, ask for clarification before continuing.
2. Write failing tests for the Acceptance Criteria.
3. Ensure the tests fail expectedly. They should not fail due to collection or fixture setup errors.
4. Write code to make all tests pass.
5. Check all tests pass.
6. Check all linting and formatting errors are fixed.

# Running Tests
1. Run unit tests
2. Run integration tests by first resetting the database then running

All tests must pass.

# Linting and Formatting
1. Run linting and formatting.
2. Fix all linting and formatting errors.
3. Run linting and formatting again to ensure all errors are fixed.

This must pass with no errors.