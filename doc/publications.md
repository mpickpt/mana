---
title: Publications
nav_order: 6
description: "Papers about MANA, and how to cite MANA."
---

# Publications

## How to cite MANA

If you use MANA in a publication, please cite the original MANA paper:

> Rohan Garg, Gregory Price, and Gene Cooperman. "MANA for MPI: MPI-Agnostic
> Network-Agnostic Transparent Checkpointing." In *Proceedings of the 28th
> International Symposium on High-Performance Parallel and Distributed
> Computing (HPDC '19)*, pages 49–60, 2019.

{% raw %}
```bibtex
@inproceedings{garg2019mana,
  author    = {Rohan Garg and Gregory Price and Gene Cooperman},
  title     = {{MANA} for {MPI}: {MPI}-Agnostic Network-Agnostic
               Transparent Checkpointing},
  booktitle = {Proceedings of the 28th International Symposium on
               High-Performance Parallel and Distributed Computing (HPDC)},
  pages     = {49--60},
  year      = {2019}
}
```
{% endraw %}

## Papers about MANA

These papers describe later versions of MANA:

- Yao Xu and Gene Cooperman. "Enabling Practical Transparent Checkpointing for
  MPI: A Topological Sort Approach." *IEEE International Conference on Cluster
  Computing (Cluster '24)*, 2024. This paper describes the algorithm that
  drains the collective calls before a checkpoint. MANA calls it the
  Collective Clock.
- Yao Xu, Leonid Belyaev, Twinkle Jain, Derek Schafer, Anthony Skjellum, and
  Gene Cooperman. "Implementation-Oblivious Transparent Checkpoint-Restart for
  MPI." *SuperCheck Workshop at SC '23*, 2023. This paper describes a version
  of MANA that is near production quality.
- Yao Xu, Zhengji Zhao, Rohan Garg, Harsh Khetawat, Rebecca Hartman-Baker, and
  Gene Cooperman. "MANA-2.0: A Future-Proof Design for Transparent
  Checkpointing of MPI at Scale." *SuperCheck Workshop at SC '21*, 2021.

## DMTCP

MANA is a plugin of DMTCP. For the papers about DMTCP, see the
[DMTCP publications](https://dmtcp.github.io/publications.html).
