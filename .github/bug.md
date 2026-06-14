- [ ] I have checked the known issues to makre sure there are no duplicates of the same issue.

name: Bug report
description: Report bugs here.
labels: [bug]
body:
  - type: textarea
    id: description
    attributes:
      label: "Describe your bug here. And how to reproduce it."
    validations:
      required: true

description: Expected behavior.
body:
  - type: textarea
    id: description
    attributes:
      label: "Describe your bug here. And how to reproduce it."
    validations:
      required: true

### Screenshots or videos:
<!-- If applicable, add screenshots or videos to help explain your problem or suggestion. -->

  - type: dropdown
    id: btarget
    attributes:
      label: "Which operating system are you using?"
      options:
        - "Windows"
        - "Linux"
        - "Mac"
        - "Nintendo Switch"
        - "Steam Deck"
    validations:
      required: true

### Additional Information:
<!-- Add any other context about the problem or suggestion here. -->
