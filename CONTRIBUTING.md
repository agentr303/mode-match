# Contributing to Mode Match

Thanks for helping. Bug reports and feature ideas are welcome as issues. Before opening a pull request, please read
the terms below: they are a condition of merging.

## Why there are contributor terms

Mode Match is released under the GNU AGPLv3 (see [LICENSE](LICENSE)). Some of its code, mainly the key detector in
`src/analysis/nuvo/`, is shared with a separate closed-source product by the same author. To keep that possible,
the maintainer needs the right to use contributions under other licence terms as well, not only the AGPLv3.

## Contributor terms

By submitting a contribution (a pull request, patch or any other code, documentation or asset) you agree that:

1. **You have the right to submit it.** It is your own original work, or you have permission to submit it under
   these terms, and it doesn't include code under a licence incompatible with them.
2. **It is licensed under the AGPLv3**, like the rest of the project.
3. **You also grant the project maintainer** a perpetual, worldwide, non-exclusive, royalty-free, irrevocable
   licence to use, copy, modify, distribute, sublicense and relicense your contribution, including in closed-source
   and commercial products, under any terms.
4. **You keep your copyright.** You stay free to use your own contribution however you like.
5. **Patents:** you grant the same recipients a licence to any patent claims you own that your contribution
   necessarily infringes, for the purposes above.

To confirm, tick the box in the pull request template, or add this line to your pull request description:

> I have read CONTRIBUTING.md and agree to the contributor terms.

Pull requests without it can't be merged.

## Practical notes

- Build and test steps are in the [README](README.md). Please run `ModeMatchTests` and say in the PR that it passes.
- Keep third-party code out unless its licence is listed as compatible in
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md); ask in an issue first.
- Don't commit audio recordings: test songs are not shipped.
