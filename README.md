# mruby-semlock
Semlock class
## install by mrbgems
- add conf.gem line to `build_config.rb`

```ruby
MRuby::Build.new do |conf|

    # ... (snip) ...

    conf.gem :github => 'tszki/mruby-semlock'
end
```
## example
```ruby
semlock = Semlock.new "./sample.rb", 0, 1, 0600  # key_str, project_num, semaphore_num, permission
semlock.lock(0)  # lock semaphore 0
semlock.unlock(0)  # unlock semaphore 0
lock_status = semlock.trylock(0)  # try to lock semaphore 0
semlock.unlock(0) if lock_status  # unlock semaphore 0 if the previos lock succeeded
semlock.remove  # remove semaphore
```

## License
under the MIT License:
- see LICENSE file
