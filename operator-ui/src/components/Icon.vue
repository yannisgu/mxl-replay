<script setup lang="ts">
defineProps<{ name: string; fill?: boolean }>();
</script>

<template>
  <svg class="i" :class="{ fill }" aria-hidden="true"><use :href="`#i-${name}`" /></svg>
</template>
